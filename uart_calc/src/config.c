/*
 * config.c - 讀取與驗證 config.txt、管理 UART 帳號表
 */
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#define MAX_ACCOUNTS 32
static account_t s_accounts[MAX_ACCOUNTS];  /* 帳號表 (由 USERS 載入)，只有本模組能直接存取 */
static int s_naccounts;                     /* 帳號表中實際的帳號數 */

/*
 * 解析 USERS 欄位，格式：帳號:密碼:角色,帳號:密碼:角色,...
 * 例如：alice:alice123:operator,bob:bob123:guest
 * 成功回傳 0，格式錯誤回傳 -1。
 */
static int parse_users(const char *v) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", v);   /* 複製一份，因為 strtok_r 會修改字串 */
    s_naccounts = 0;
    /* 以逗號切開每一組帳號 */
    for (char *save = NULL, *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *name = trim(tok), *pass, *role;
        if (!*name) continue;   /* 空的項目略過 */
        /* 找第一個 ':' (帳號與密碼的分隔) 與第二個 ':' (密碼與角色的分隔) */
        if (!(pass = strchr(name, ':')) || !(role = strchr(pass + 1, ':'))) return -1;
        *pass++ = '\0';   /* 把 ':' 改成 '\0' 截斷帳號，指標往後移到密碼開頭 */
        *role++ = '\0';   /* 同上，截斷密碼，指標移到角色開頭 */
        int r = role_from_name(trim(role));
        if (r < 0 || s_naccounts >= MAX_ACCOUNTS) return -1;   /* 角色名稱錯誤或帳號太多 */
        account_t *a = &s_accounts[s_naccounts++];
        snprintf(a->name, sizeof a->name, "%s", trim(name));
        snprintf(a->pass, sizeof a->pass, "%s", trim(pass));
        a->role = r;
    }
    return s_naccounts > 0 ? 0 : -1;   /* 至少要有一個帳號 */
}

/* 題目規定的必要欄位：缺少任何一個，程式都不得啟動 */
static const char *const REQUIRED_KEYS[] = {
    "TCP_PORT", "UART_DEVICE", "UART_BAUD", "LOG_FILE", "MAX_LOG_LINES",
    "ALLOW_GUEST", "ALLOW_OPERATOR", "ALLOW_ADMIN", "ADMIN_PASSWORD", "UART_TIMEOUT_SEC",
};
#define N_REQUIRED (int)(sizeof REQUIRED_KEYS / sizeof REQUIRED_KEYS[0])  /* 必要欄位數量 */
#define IDX_LOG_FILE 3   /* REQUIRED_KEYS 中 LOG_FILE 的位置 */

int config_load(const char *path, config_t *cfg) {
    memset(cfg, 0, sizeof *cfg);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "[config] cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    int found[N_REQUIRED] = {0};   /* found[i]=1 表示 REQUIRED_KEYS[i] 有出現且值合法 */
    int have_users = 0, err = 0, lineno = 0;
    char line[1024];

    while (fgets(line, sizeof line, f)) {   /* 一次讀一行 */
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#') continue;     /* 空行或註解行略過 */
        char *eq = strchr(s, '=');
        if (!eq) {                          /* 沒有 '=' 就是格式錯誤 */
            fprintf(stderr, "[config] line %d: missing '='\n", lineno);
            err = 1;
            continue;
        }
        *eq = '\0';                         /* 在 '=' 處切開：左邊是 key，右邊是 value */
        char *k = trim(s), *v = trim(eq + 1);

        /* 查這個 key 是不是必要欄位之一，記下它的編號 */
        int idx = -1;
        for (int i = 0; i < N_REQUIRED; i++)
            if (strcmp(k, REQUIRED_KEYS[i]) == 0) idx = i;

        /* 依 key 解析 value，並檢查合法範圍；bad != 0 表示值不合法 */
        int bad = 0;
        if (strcmp(k, "TCP_PORT") == 0)              bad = parse_int(v, &cfg->tcp_port, 1, 65535);
        else if (strcmp(k, "UART_DEVICE") == 0)      { bad = !*v; snprintf(cfg->uart_device, sizeof cfg->uart_device, "%s", v); }
        else if (strcmp(k, "UART_BAUD") == 0)        bad = parse_int(v, &cfg->uart_baud, 1, 4000000);
        else if (strcmp(k, "LOG_FILE") == 0)         { bad = !*v; snprintf(cfg->log_file, sizeof cfg->log_file, "%s", v); }
        else if (strcmp(k, "MAX_LOG_LINES") == 0)    bad = parse_int(v, &cfg->max_log_lines, 10, 10000000);
        else if (strcmp(k, "ALLOW_GUEST") == 0)      bad = parse_int(v, &cfg->allow[ROLE_GUEST], 0, 1);
        else if (strcmp(k, "ALLOW_OPERATOR") == 0)   bad = parse_int(v, &cfg->allow[ROLE_OPERATOR], 0, 1);
        else if (strcmp(k, "ALLOW_ADMIN") == 0)      bad = parse_int(v, &cfg->allow[ROLE_ADMIN], 0, 1);
        else if (strcmp(k, "ADMIN_PASSWORD") == 0)   { bad = !*v; snprintf(cfg->admin_password, sizeof cfg->admin_password, "%s", v); }
        /* VTIME 最大 255 (=25.5 秒)，所以逾時限制在 1~25 秒 */
        else if (strcmp(k, "UART_TIMEOUT_SEC") == 0) bad = parse_int(v, &cfg->uart_timeout_sec, 1, 25);
        else if (strcmp(k, "USERS") == 0)            { bad = parse_users(v); have_users = !bad; }  /* 選填欄位 */
        else fprintf(stderr, "[config] line %d: unknown key '%s' (ignored)\n", lineno, k);  /* 不認得的 key 只警告 */

        if (bad) {
            fprintf(stderr, "[config] line %d: invalid value for %s: '%s'\n", lineno, k, v);
            err = 1;
        } else if (idx >= 0) {
            found[idx] = 1;   /* 必要欄位已正確設定 */
        }
    }
    fclose(f);

    /* 檢查每個必要欄位是否都有出現 */
    for (int i = 0; i < N_REQUIRED; i++) {
        if (!found[i]) {
            fprintf(stderr, "[config] missing required key: %s\n", REQUIRED_KEYS[i]);
            err = 1;
        }
    }
    /* 題目規定日誌檔固定為 log.txt */
    if (found[IDX_LOG_FILE] && strcmp(cfg->log_file, REQUIRED_LOG_FILE) != 0) {
        fprintf(stderr, "[config] LOG_FILE must be %s (got %s)\n", REQUIRED_LOG_FILE, cfg->log_file);
        err = 1;
    }
    if (!have_users && !err) {
        /* 未設定 USERS 時使用預設帳號 */
        parse_users("alice:alice123:operator,bob:bob123:guest,root:root123:admin");
    }
    return err ? -1 : 0;
}

const account_t *config_find_account(const char *name, const char *pass) {
    for (int i = 0; i < s_naccounts; i++)
        if (strcmp(s_accounts[i].name, name) == 0 && strcmp(s_accounts[i].pass, pass) == 0)
            return &s_accounts[i];
    return NULL;
}
