/*
 * web.c - 網頁管理端：HTTP 解析、登入 session、頁面、JSON API
 */
#include "web.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "admin_cmd.h"
#include "log.h"
#include "role.h"
#include "state.h"
#include "strbuf.h"
#include "util.h"

static const config_t *s_cfg;   /* 設定值 (管理密碼、頁尾顯示) */
static int s_listen_fd = -1;    /* 監聽用的 socket */

/* ======================================================================
 * 網頁登入 session
 * ====================================================================== */
#define MAX_WEB_SESSIONS 8      /* 最多同時 8 個瀏覽器登入 */
#define WEB_SESSION_IDLE 1800   /* 閒置 1800 秒 (30 分鐘) 自動登出 */
typedef struct {
    char token[33];     /* 32 個十六進位字元的隨機 session id；空字串表示此欄位未使用 */
    time_t last;        /* 最後一次使用時間 */
    char flash[131072]; /* 下一次顯示頁面時的命令輸出 */
    /* flash 的用途：POST /cmd 執行完命令後，把輸出暫存在這裡並重新導向到 GET /，
     * 主頁顯示後就清空。這樣使用者按 F5 重新整理時不會重複送出命令 (PRG 模式)。 */
} web_session_t;
static web_session_t s_ws[MAX_WEB_SESSIONS];   /* 只在網頁執行緒中使用，不需要上鎖 */

/*
 * 依 token 找出對應的 session，並順便清除已過期的 session。
 * 找到會更新最後使用時間；tok 傳 NULL 時只做清除過期的動作。
 */
static web_session_t *ws_find(const char *tok) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_WEB_SESSIONS; i++) {
        web_session_t *w = &s_ws[i];
        if (!w->token[0]) continue;   /* 空欄位 */
        if (now - w->last > WEB_SESSION_IDLE) { w->token[0] = '\0'; continue; }   /* 過期 → 作廢 */
        if (tok && strcmp(w->token, tok) == 0) { w->last = now; return w; }
    }
    return NULL;
}

/* 建立新的登入 session：找空欄位 (都滿了就覆蓋最久沒用的)，產生隨機 token */
static web_session_t *ws_create(void) {
    web_session_t *slot = &s_ws[0];
    ws_find(NULL); /* 清除過期 */
    for (int i = 0; i < MAX_WEB_SESSIONS; i++) {
        if (!s_ws[i].token[0]) { slot = &s_ws[i]; break; }   /* 找到空欄位 */
        if (s_ws[i].last < slot->last) slot = &s_ws[i];      /* 否則記住最舊的 */
    }
    /* 從 /dev/urandom 取 16 bytes 隨機數 (無法讀取時退而使用 rand()) */
    unsigned char rnd[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, rnd, sizeof rnd) != sizeof rnd) {
        for (int i = 0; i < 16; i++) rnd[i] = (unsigned char)(rand() ^ time(NULL));
    }
    if (fd >= 0) close(fd);
    /* 轉成 32 個十六進位字元當作 token */
    for (int i = 0; i < 16; i++) sprintf(slot->token + i * 2, "%02x", rnd[i]);
    slot->last = time(NULL);
    slot->flash[0] = '\0';
    return slot;
}

/* ======================================================================
 * HTTP 工具
 * ====================================================================== */

/* 把資料完整送出 (send 可能一次送不完)。
 * MSG_NOSIGNAL：對方已關閉連線時不要產生 SIGPIPE 讓程式結束 */
static void send_all(int fd, const char *b, size_t n) {
    while (n > 0) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return;
        }
        b += w;
        n -= (size_t)w;
    }
}

/*
 * 送出一個完整的 HTTP 回應 (狀態列 + 標頭 + 內容)。
 *   status    : 如 "200 OK"
 *   ctype     : Content-Type，如 "text/html; charset=utf-8"
 *   extra_hdr : 額外標頭 (每行需以 \r\n 結尾)，可為 NULL
 * 每個連線只處理一個請求 (Connection: close)，實作最簡單。
 */
static void http_send(int fd, const char *status, const char *ctype,
                      const char *extra_hdr, const char *body, size_t blen) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n",   /* 標頭結尾要空一行 */
                     status, ctype, blen, extra_hdr ? extra_hdr : "");
    send_all(fd, hdr, (size_t)n);
    send_all(fd, body, blen);
}

/* 送出 303 重新導向，讓瀏覽器改用 GET 開啟 loc */
static void http_redirect(int fd, const char *loc, const char *extra_hdr) {
    char hdr[512];
    snprintf(hdr, sizeof hdr, "Location: %s\r\n%s", loc, extra_hdr ? extra_hdr : "");
    http_send(fd, "303 See Other", "text/plain", hdr, "", 0);
}

/* 十六進位字元轉數值 ('a' → 10)；不是十六進位字元回傳 -1 */
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * 從 application/x-www-form-urlencoded 取出欄位 (HTML 表單 POST 的格式)。
 * 例：body = "cmd=lock+operator&x=1"，key = "cmd" → out = "lock operator"
 * 解碼規則：'+' → 空白，"%XX" → 對應的字元 (如 %2B → '+')。
 * 找到回傳 0，找不到回傳 -1 (out 為空字串)。
 */
static int form_get(const char *body, const char *key, char *out, size_t n) {
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {   /* 找到 "key=" */
            p += klen + 1;
            size_t j = 0;
            while (*p && *p != '&' && j + 1 < n) {             /* 讀到 '&' 或結尾為止 */
                if (*p == '+') { out[j++] = ' '; p++; }
                else if (*p == '%' && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
                    out[j++] = (char)(hexval(p[1]) * 16 + hexval(p[2]));
                    p += 3;
                } else out[j++] = *p++;
            }
            out[j] = '\0';
            return 0;
        }
        p = strchr(p, '&');   /* 跳到下一個欄位 */
        if (p) p++;
    }
    out[0] = '\0';
    return -1;
}

/*
 * 在 HTTP 標頭中尋找指定名稱的標頭 (名稱不分大小寫)，把值複製到 out。
 * 找到回傳 out，找不到回傳 NULL。
 * 每個標頭都在 "\r\n" 之後開始，所以逐一找 "\r\n" 並比對後面的名稱。
 */
static const char *find_header(const char *hdrs, const char *name, char *out, size_t n) {
    size_t nl = strlen(name);
    const char *p = hdrs;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;   /* 跳過 \r\n，p 指向下一個標頭的開頭 */
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ') p++;   /* 跳過冒號後的空白 */
            size_t j = 0;
            while (*p && *p != '\r' && j + 1 < n) out[j++] = *p++;
            out[j] = '\0';
            return out;
        }
    }
    return NULL;
}

/* 從 Cookie 標頭中取出 sid 的值，例如 "Cookie: a=1; sid=abcd" → "abcd" */
static void get_cookie_sid(const char *hdrs, char *out, size_t n) {
    char cookie[1024];
    out[0] = '\0';
    if (!find_header(hdrs, "Cookie", cookie, sizeof cookie)) return;
    char *p = strstr(cookie, "sid=");
    /* 確保找到的是完整的 "sid="，而不是 "xsid=" 這種別的 cookie 名稱的一部分 */
    while (p && p != cookie && p[-1] != ' ' && p[-1] != ';') p = strstr(p + 1, "sid=");
    if (!p) return;
    p += 4;   /* 跳過 "sid=" */
    size_t j = 0;
    while (*p && *p != ';' && j + 1 < n) out[j++] = *p++;
    out[j] = '\0';
}

/* ======================================================================
 * 頁面
 * ====================================================================== */

/* 頁面共用的 CSS 樣式 (含深色模式：依照作業系統設定自動切換) */
static const char *const CSS =
    "<style>"
    ":root{--bg:#f4f6f9;--card:#fff;--fg:#1d2433;--mute:#6b7385;--line:#dde2ea;"
    "--accent:#2563eb;--ok:#15803d;--bad:#b91c1c;--term:#0f172a;--termfg:#d1e7ff}"
    "@media(prefers-color-scheme:dark){:root{--bg:#0f1420;--card:#171e2d;--fg:#e5e9f0;"
    "--mute:#97a0b3;--line:#2a3347;--accent:#60a5fa;--ok:#4ade80;--bad:#f87171;--term:#060a12}}"
    "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);"
    "font:15px/1.5 system-ui,-apple-system,'Segoe UI','Noto Sans TC',sans-serif}"
    ".wrap{max-width:980px;margin:0 auto;padding:20px 16px}"
    "h1{font-size:20px;margin:0}header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;gap:8px;flex-wrap:wrap}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin-bottom:16px}"
    ".card h2{font-size:15px;margin:0 0 12px;color:var(--mute);font-weight:600}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}"
    ".role{border:1px solid var(--line);border-radius:8px;padding:12px}"
    ".role b{font-size:16px}.pill{display:inline-block;padding:1px 8px;border-radius:99px;font-size:12px;font-weight:600;margin-left:6px}"
    ".on{background:color-mix(in srgb,var(--ok) 15%,transparent);color:var(--ok)}"
    ".off{background:color-mix(in srgb,var(--bad) 15%,transparent);color:var(--bad)}"
    "button,input{font:inherit}button{background:var(--accent);color:#fff;border:0;border-radius:6px;padding:6px 14px;cursor:pointer}"
    "button.danger{background:var(--bad)}button.ghost{background:transparent;color:var(--fg);border:1px solid var(--line)}"
    "form{display:inline}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:8px}"
    "input[type=text],input[type=password]{padding:6px 10px;border:1px solid var(--line);border-radius:6px;background:var(--bg);color:var(--fg);flex:1;min-width:0}"
    "pre{background:var(--term);color:var(--termfg);padding:12px;border-radius:8px;overflow-x:auto;margin:0;font:13px/1.45 ui-monospace,Menlo,Consolas,monospace;white-space:pre}"
    ".mute{color:var(--mute);font-size:13px}.err{color:var(--bad)}"
    ".login{max-width:360px;margin:12vh auto}"
    "</style>";

/* 登入頁：一個密碼輸入框；err 不為 NULL 時顯示錯誤訊息 */
static void page_login(int fd, const char *err) {
    sb_t s;
    sb_init(&s);
    sb_printf(&s, "<!doctype html><html lang=zh-Hant><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>UART Calc Admin</title>%s</head><body><div class='wrap login'>"
                  "<div class=card><h1>UART Calculator 管理端</h1><p class=mute>Admin Password:</p>"
                  "<form method=post action=/login class=row style='display:flex'>"
                  "<input type=password name=password autofocus>"
                  "<button>登入</button></form>", CSS);
    if (err) sb_printf(&s, "<p class=err>%s</p>", err);
    sb_printf(&s, "</div></div></body></html>");
    http_send(fd, "200 OK", "text/html; charset=utf-8", NULL, s.buf, s.len);
    sb_free(&s);
}

/*
 * 管理主頁。每個按鈕都是一個小表單，按下時 POST /cmd 並帶上對應的命令文字，
 * 所以按鈕與命令列輸入共用同一個 admin_exec() 處理邏輯。
 */
static void page_dashboard(int fd, web_session_t *ws) {
    sb_t s;
    sb_init(&s);
    /* 頁首與登出按鈕 (送出 quit 命令) */
    sb_printf(&s, "<!doctype html><html lang=zh-Hant><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>UART Calc Admin</title>%s</head><body><div class=wrap>"
                  "<header><h1>UART Calculator 管理端</h1>"
                  "<form method=post action=/cmd><input type=hidden name=cmd value=quit>"
                  "<button class=ghost>登出 (quit)</button></form></header>", CSS);

    /* UART 狀態 (由 JS 每 2 秒更新) */
    sb_printf(&s, "<div class=card><h2>UART Session <span class=mute id=upd></span></h2>"
                  "<div id=uart class=row style='margin:0'>載入中…</div>"
                  "<div class=row><form method=post action=/cmd><input type=hidden name=cmd value='kick uart'>"
                  "<button class=danger>kick uart</button></form></div></div>");

    /* 角色控制：每個角色一個區塊，有狀態標籤與 lock / unlock 按鈕 */
    sb_printf(&s, "<div class=card><h2>計算機權限 (角色)</h2><div class=grid>");
    for (int i = 0; i < ROLE_COUNT; i++) {
        sb_printf(&s, "<div class=role><b>%s</b><span class=pill id=pill_%s>…</span>"
                      "<div class=mute>%s</div><div class=row>"
                      "<form method=post action=/cmd><input type=hidden name=cmd value='lock %s'><button class=danger>lock</button></form>"
                      "<form method=post action=/cmd><input type=hidden name=cmd value='unlock %s'><button>unlock</button></form>"
                      "</div></div>",
                  ROLE_NAME[i], ROLE_NAME[i],
                  i == ROLE_GUEST ? "登入可用 help/status，永遠無法使用計算機" :
                  i == ROLE_OPERATOR ? "可使用計算機" : "可使用計算機 (完整管理)",
                  ROLE_NAME[i], ROLE_NAME[i]);
    }
    sb_printf(&s, "</div></div>");

    /* 命令列：文字輸入框 + 常用命令快捷按鈕 + 輸出區 (<pre>) */
    sb_printf(&s, "<div class=card><h2>管理命令</h2>"
                  "<form method=post action=/cmd class=row style='display:flex;margin:0 0 12px'>"
                  "<input type=text name=cmd placeholder='status / lock operator / showlog 20 / help' autocomplete=off>"
                  "<button>執行</button></form><div class=row style='margin:0 0 12px'>");
    const char *quick[] = {"status", "showlog", "showlog 50", "help"};
    for (int i = 0; i < 4; i++)
        sb_printf(&s, "<form method=post action=/cmd><input type=hidden name=cmd value='%s'>"
                      "<button class=ghost>%s</button></form>", quick[i], quick[i]);
    sb_printf(&s, "</div><pre>");
    /* 顯示上一個命令的輸出 (要做 HTML 跳脫，因為 log 可能含有使用者輸入) */
    if (ws->flash[0]) sb_html(&s, ws->flash);
    else sb_printf(&s, "TCP Control Ready (web)\n輸入命令或點選按鈕");
    sb_printf(&s, "</pre></div>");

    /* 頁尾顯示目前使用中的設定值 */
    sb_printf(&s,
        "<p class=mute>設定: port=%d, UART=%s @ %d, timeout=%ds, log=%s (max %d lines)</p>",
        s_cfg->tcp_port, s_cfg->uart_device, s_cfg->uart_baud, s_cfg->uart_timeout_sec,
        s_cfg->log_file, s_cfg->max_log_lines);

    /* JavaScript：每 2 秒呼叫 /api/status，更新 UART 狀態與角色標籤 (不用重新整理頁面)。
     * 收到 401 表示登入已失效，跳回登入頁。esc() 用來做 HTML 跳脫。 */
    sb_printf(&s,
        "<script>"
        "const R=['guest','operator','admin'];"
        "function esc(t){return String(t).replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]))}"
        "async function poll(){try{const r=await fetch('/api/status',{cache:'no-store'});"
        "if(r.status==401){location.href='/';return}const j=await r.json();"
        "document.getElementById('uart').innerHTML=j.uart.active?"
        "`user=<b>${esc(j.uart.user)}</b> role=<b>${esc(j.uart.role)}</b> <span class='pill on'>active</span> <span class=mute>since ${esc(j.uart.since)}</span>`:"
        "`<span class='pill off'>idle</span> <span class=mute>目前沒有 UART 使用者</span>`;"
        "for(const k of R){const e=document.getElementById('pill_'+k);const on=j.roles[k];"
        "e.textContent=on?'enabled':'disabled';e.className='pill '+(on?'on':'off')}"
        "document.getElementById('upd').textContent='· 更新於 '+new Date().toLocaleTimeString()"
        "}catch(e){document.getElementById('upd').textContent='· 連線中斷'}}"
        "poll();setInterval(poll,2000);"
        "</script></div></body></html>");
    ws->flash[0] = '\0';   /* 輸出已顯示過，清空 */
    http_send(fd, "200 OK", "text/html; charset=utf-8", NULL, s.buf, s.len);
    sb_free(&s);
}

/*
 * GET /api/status：回傳 JSON，例如
 *   {"uart":{"active":true,"user":"alice","role":"operator","since":"..."},
 *    "roles":{"guest":true,"operator":false,"admin":true}}
 * 這是頁面自動輪詢用的，不寫入 log (否則每 2 秒一筆會塞滿日誌)。
 */
static void api_status(int fd) {
    state_snapshot_t st;
    state_snapshot(&st);
    char since[32] = "-";
    if (st.uart_active) {
        struct tm tm;
        localtime_r(&st.uart_login_time, &tm);
        strftime(since, sizeof since, "%Y-%m-%d %H:%M:%S", &tm);
    }
    sb_t s;
    sb_init(&s);
    sb_printf(&s, "{\"uart\":{\"active\":%s,\"user\":", st.uart_active ? "true" : "false");
    sb_json_str(&s, st.uart_active ? st.uart_user : "-");
    sb_printf(&s, ",\"role\":");
    sb_json_str(&s, st.uart_active ? ROLE_NAME[st.uart_role] : "-");
    sb_printf(&s, ",\"since\":\"%s\"},\"roles\":{", since);
    for (int i = 0; i < ROLE_COUNT; i++)
        sb_printf(&s, "%s\"%s\":%s", i ? "," : "", ROLE_NAME[i], st.role_enabled[i] ? "true" : "false");
    sb_printf(&s, "}}");
    http_send(fd, "200 OK", "application/json", NULL, s.buf, s.len);
    sb_free(&s);
}

/* ======================================================================
 * 請求處理
 * ====================================================================== */

/*
 * 處理一個 HTTP 連線：讀取請求 → 解析 → 依路徑分派 → 送出回應。
 * peer 是對方的 IP 字串 (寫入 log 用)。
 */
static void handle_client(int fd, const char *peer) {
    char req[16384];   /* 整個請求 (標頭 + 內容) 的緩衝區 */
    size_t len = 0;
    char *hdr_end = NULL;
    /* 1. 持續接收，直到看到標頭結尾 "\r\n\r\n" (空行) */
    while (len + 1 < sizeof req) {
        ssize_t n = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (n <= 0) return;   /* 連線中斷或逾時 */
        len += (size_t)n;
        req[len] = '\0';
        if ((hdr_end = strstr(req, "\r\n\r\n"))) break;
    }
    if (!hdr_end) return;   /* 標頭太長，放棄 */
    char *body = hdr_end + 4;   /* 空行後面就是內容 (body) */
    *hdr_end = '\0';            /* 把標頭與內容切開 */

    /* 2. 依 Content-Length 讀完內容 (POST 表單資料) */
    char clen_s[32];
    long clen = 0;
    if (find_header(req, "Content-Length", clen_s, sizeof clen_s)) clen = atol(clen_s);
    if (clen < 0 || clen > (long)(sizeof req - 1 - (size_t)(body - req))) clen = 0;   /* 不合理或太大就忽略 */
    while ((long)(len - (size_t)(body - req)) < clen) {
        ssize_t n = recv(fd, req + len, sizeof req - 1 - len, 0);
        if (n <= 0) break;
        len += (size_t)n;
    }
    req[len] = '\0';
    body[clen] = '\0';

    /* 3. 解析請求列，例如 "POST /cmd HTTP/1.1" → method="POST", path="/cmd" */
    char method[8] = "", path[256] = "";
    sscanf(req, "%7s %255s", method, path);
    char *q = strchr(path, '?');   /* 去掉 ?後面的查詢字串 */
    if (q) *q = '\0';

    /* 4. 依 Cookie 判斷是否已登入；ws == NULL 表示未登入 */
    char sid[64];
    get_cookie_sid(req, sid, sizeof sid);
    web_session_t *ws = sid[0] ? ws_find(sid) : NULL;

    /* 5. 依路徑分派 */

    /* POST /login：驗證管理密碼 */
    if (strcmp(path, "/login") == 0 && strcmp(method, "POST") == 0) {
        char pw[128];
        form_get(body, "password", pw, sizeof pw);
        if (strcmp(pw, s_cfg->admin_password) == 0) {
            /* 密碼正確：建立 session，用 Set-Cookie 把 sid 交給瀏覽器
             * HttpOnly：JavaScript 讀不到；SameSite=Strict：別的網站無法帶著這個 cookie 送請求 */
            web_session_t *n = ws_create();
            char cookie[160];
            snprintf(cookie, sizeof cookie, "Set-Cookie: sid=%.32s; Path=/; HttpOnly; SameSite=Strict\r\n", n->token);
            snprintf(n->flash, sizeof n->flash, "TCP Control Ready (web)\n");
            log_event("WEB", "admin", "login", "OK", "from %s", peer);
            http_redirect(fd, "/", cookie);
        } else {
            log_event("WEB", "admin", "login", "FAIL", "wrong password from %s", peer);
            page_login(fd, "密碼錯誤");
        }
        return;
    }

    /* GET /：已登入顯示管理主頁，否則顯示登入頁 */
    if (strcmp(path, "/") == 0 && strcmp(method, "GET") == 0) {
        if (ws) page_dashboard(fd, ws);
        else page_login(fd, NULL);
        return;
    }

    /* GET /api/status：JSON 狀態 (需登入) */
    if (strcmp(path, "/api/status") == 0) {
        if (!ws) { http_send(fd, "401 Unauthorized", "application/json", NULL, "{}", 2); return; }
        api_status(fd);
        return;
    }

    /* POST /cmd：執行管理命令 (需登入) */
    if (strcmp(path, "/cmd") == 0 && strcmp(method, "POST") == 0) {
        if (!ws) {
            log_event("WEB", "-", "command", "FAIL", "not logged in from %s", peer);
            http_redirect(fd, "/", NULL);
            return;
        }
        char cmd[256];
        form_get(body, "cmd", cmd, sizeof cmd);
        char *c = trim(cmd);
        sb_t out;
        sb_init(&out);
        sb_printf(&out, "admin> %s\n", c);   /* 模仿終端機，先顯示輸入的命令 */
        int quit = admin_exec(c, &out);
        snprintf(ws->flash, sizeof ws->flash, "%s", out.buf);   /* 存起來，等重新導向後顯示 */
        sb_free(&out);
        if (quit) {
            /* quit：作廢 session 並刪除瀏覽器的 cookie (Max-Age=0) */
            ws->token[0] = '\0';
            http_redirect(fd, "/", "Set-Cookie: sid=; Path=/; Max-Age=0\r\n");
        } else {
            http_redirect(fd, "/", NULL);
        }
        return;
    }

    /* 其他路徑 */
    http_send(fd, "404 Not Found", "text/plain", NULL, "Not Found\n", 10);
}

/* ======================================================================
 * 對外介面
 * ====================================================================== */

int web_init(const config_t *cfg) {
    s_cfg = cfg;
    int fd = socket(AF_INET, SOCK_STREAM, 0);   /* IPv4、TCP */
    if (fd < 0) return -1;
    int one = 1;
    /* SO_REUSEADDR：程式重啟時可以馬上重新使用同一個 port，
     * 不用等舊連線的 TIME_WAIT 狀態結束 */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);   /* 0.0.0.0：接受任何網卡的連線 */
    a.sin_port = htons((uint16_t)cfg->tcp_port);   /* htons：轉成網路位元組順序 */
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 16) < 0) {
        fprintf(stderr, "[web] cannot listen on port %d: %s\n", cfg->tcp_port, strerror(errno));
        close(fd);
        return -1;
    }
    s_listen_fd = fd;
    return 0;
}

void web_close(void) {
    if (s_listen_fd >= 0) close(s_listen_fd);
    s_listen_fd = -1;
}

/*
 * 等待連線 → 處理 → 關閉，一次處理一個連線。
 * 用 select() 等待最多 1 秒，這樣即使沒有人連線，也能定期檢查 app_running() 以便結束。
 */
void *web_thread(void *arg) {
    (void)arg;
    while (app_running()) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(s_listen_fd, &rf);
        struct timeval tv = {1, 0};   /* 等待 1 秒 */
        int r = select(s_listen_fd + 1, &rf, NULL, NULL, &tv);
        if (r <= 0) continue;         /* 逾時或被 signal 中斷 → 回去檢查 app_running() */
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        int cfd = accept(s_listen_fd, (struct sockaddr *)&cli, &cl);   /* 接受連線 */
        if (cfd < 0) continue;
        /* 收送逾時 5 秒，避免惡意或異常的連線卡住整個網頁服務 */
        struct timeval to = {5, 0};
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
        char peer[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &cli.sin_addr, peer, sizeof peer);   /* 取得對方 IP 字串 */
        handle_client(cfd, peer);
        close(cfd);
    }
    return NULL;
}
