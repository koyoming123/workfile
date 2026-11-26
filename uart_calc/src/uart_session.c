/*
 * uart_session.c - UART 端的登入流程與計算機互動
 */
#include "uart_session.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "calc.h"
#include "config.h"
#include "log.h"
#include "role.h"
#include "state.h"
#include "uart_hal.h"
#include "util.h"

/* readline() 的特殊回傳值 */
#define RL_SHUTDOWN (-1)   /* 程式要結束了 (或讀取發生錯誤) */
#define RL_KICKED   (-2)   /* 被網頁管理端踢除 */

/*
 * 從 UART 讀一行輸入 (遇到 Enter 結束)。
 *   buf / size : 存放結果的緩衝區
 *   echo       : 1=回顯使用者打的字；0=不回顯 (輸入密碼時用)
 *   in_session : 1=已登入狀態，逾時時要檢查是否被踢除
 * 回傳：讀到的字元數 (>=0)，或 RL_SHUTDOWN / RL_KICKED。
 *
 * 因為 UART 設成 raw 模式，PuTTY 送來的每個按鍵都會直接收到，
 * 所以回顯、Backspace、Enter 都要由這裡自己處理 (這就是一個簡單的行編輯器)。
 */
static int readline(char *buf, size_t size, int echo, int in_session) {
    size_t len = 0;
    /* 記住上一個字元是否為 '\r'。有些終端機按 Enter 送 "\r\n"，
     * 收到 '\r' 時已經結束一行，緊接著的 '\n' 要忽略，否則會多出一個空行。
     * 宣告為 static 是因為 '\r' 與 '\n' 可能分在兩次呼叫中收到。 */
    static int last_cr = 0;
    for (;;) {
        if (!app_running()) return RL_SHUTDOWN;   /* 收到關機訊號 */
        unsigned char c;
        int r = uart_hal_getc(&c);   /* 最多等 UART_TIMEOUT_SEC 秒 */
        if (r < 0) return RL_SHUTDOWN;
        if (r == 0) { /* 逾時 (UART_TIMEOUT_SEC) */
            /* 沒有輸入：趁這個空檔檢查網頁端是否要求踢除 */
            if (in_session && state_kick_requested()) return RL_KICKED;
            continue;
        }
        if (c == '\n' && last_cr) { last_cr = 0; continue; } /* CRLF：忽略 '\r' 後面的 '\n' */
        last_cr = (c == '\r');
        if (c == '\r' || c == '\n') {         /* Enter：一行結束 */
            buf[len] = '\0';
            uart_hal_puts("\r\n");            /* 終端機換行要送 \r\n */
            return (int)len;
        }
        if (c == 0x7f || c == 0x08) {             /* Backspace (PuTTY 預設送 0x7f) */
            if (len > 0) {
                len--;
                /* "\b \b"：游標左移、用空白蓋掉字元、游標再左移，畫面上就刪掉一個字 */
                if (echo) uart_hal_puts("\b \b");
            }
        } else if (c == 0x03) {                   /* Ctrl-C 清除本行 */
            len = 0;
            uart_hal_puts("^C\r\n");
            buf[0] = '\0';
            return 0;
        } else if (c >= 0x20 && c < 0x7f && len + 1 < size) {   /* 一般可顯示字元，且緩衝區還有空間 */
            buf[len++] = (char)c;
            if (echo) uart_hal_write((char *)&c, 1);   /* 回顯，讓使用者看到自己打的字 */
        }
        /* 其他控制字元 (方向鍵等) 直接忽略 */
    }
}

/* 內建指令 status：顯示自己的角色與各角色是否啟用 */
static void cmd_status(const char *user, int role) {
    state_snapshot_t st;
    state_snapshot(&st);
    int can = (role != ROLE_GUEST) && st.role_enabled[role];   /* 自己目前能不能用計算機 */
    uart_hal_printf("user=%s role=%s calculator=%s\r\n", user, ROLE_NAME[role],
                    can ? "allowed" : "denied");
    for (int i = 0; i < ROLE_COUNT; i++)
        uart_hal_printf("%s=%s\r\n", ROLE_NAME[i], st.role_enabled[i] ? "enabled" : "disabled");
    log_event("UART", user, "status", "OK", "-");
}

/* 把輸入當成算式：先檢查權限，再計算 */
static void cmd_calc(const char *user, int role, const char *expr) {
    char res[128];
    /* 每次計算前都重新讀取角色狀態，所以網頁端 lock 之後會立刻生效 */
    int enabled = state_role_enabled(role);
    if (role == ROLE_GUEST) {
        /* guest 本來就沒有計算機權限 */
        uart_hal_puts("Permission denied\r\n");
        log_event("UART", user, "calc", "DENIED", "expr=%s role=guest no calculator permission", expr);
    } else if (!enabled) {
        /* 角色被網頁管理端鎖定 */
        uart_hal_puts("Permission denied\r\n");
        log_event("UART", user, "calc", "DENIED", "expr=%s role locked", expr);
    } else if (calc_eval(expr, res, sizeof res) == 0) {
        /* 計算成功 */
        uart_hal_printf("%s\r\n", res);
        log_event("UART", user, "calc", "OK", "expr=%s result=%s", expr, res);
    } else {
        /* 計算失敗 (語法錯誤、除以 0 等) */
        uart_hal_printf("Error: %s\r\n", res);
        log_event("UART", user, "calc", "FAIL", "expr=%s error=%s", expr, res);
    }
}

/*
 * 一個 UART 使用者登入後的完整互動 (計算機模式)，直到 quit / 被踢除 / 關機才返回。
 */
static void run_session(const account_t *acc) {
    char line[256];
    const char *user = acc->name;
    int role = acc->role;

    state_session_begin(user, role);   /* 讓網頁端知道現在誰登入 */
    log_event("UART", user, "login", "OK", "role=%s", ROLE_NAME[role]);
    uart_hal_printf("Welcome, role=%s\r\nCalculator Ready\r\n", ROLE_NAME[role]);

    const char *logout_detail = "user quit";   /* 登出原因，寫進 log */
    for (;;) {
        uart_hal_puts("> ");   /* 提示字元 */
        int r = readline(line, sizeof line, 1, 1);
        if (r == RL_KICKED) {          /* 被網頁端 kick uart */
            uart_hal_puts("\r\n*** Session terminated by admin ***\r\n");
            logout_detail = "kicked by admin";
            break;
        }
        if (r == RL_SHUTDOWN) {        /* 程式要關閉 */
            uart_hal_puts("\r\n*** System shutting down ***\r\n");
            logout_detail = "system shutdown";
            break;
        }
        char *cmd = trim(line);
        if (!*cmd) continue;   /* 空行：重新顯示提示字元 */

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            /* 內建指令 quit：登出 */
            uart_hal_puts("bye\r\n");
            break;
        } else if (strcmp(cmd, "help") == 0) {
            /* 內建指令 help：所有角色都可以用 */
            uart_hal_puts("Input arithmetic expression with + - * / and ()\r\n"
                          "Commands: help, status, quit\r\n");
            log_event("UART", user, "help", "OK", "-");
        } else if (strcmp(cmd, "status") == 0) {
            cmd_status(user, role);
        } else {
            cmd_calc(user, role, cmd);
        }
    }

    /* 離開計算機模式：記錄登出並清除共享狀態 */
    log_event("UART", user, "logout", "OK", "%s", logout_detail);
    state_session_end();
}

void *uart_session_thread(void *arg) {
    (void)arg;   /* 未使用的參數，避免編譯器警告 */
    char name[64], pass[64];
    uart_hal_puts("\r\n=== Raspberry Pi UART Calculator ===\r\n");
    while (app_running()) {
        uart_hal_puts("\r\nLogin: ");
        int r = readline(name, sizeof name, 1, 0);
        if (r < 0) break;          /* 關機 */
        char *n = trim(name);
        if (!*n) continue;         /* 沒輸入帳號就直接按 Enter：重新顯示 Login: */
        uart_hal_puts("Password: ");
        r = readline(pass, sizeof pass, 0, 0);   /* echo=0：密碼不顯示 */
        if (r < 0) break;

        const account_t *acc = config_find_account(n, pass);
        if (!acc) {
            uart_hal_puts("Login incorrect\r\n");
            log_event("UART", n, "login", "FAIL", "invalid username or password");
            sleep(1);   /* 登入失敗延遲 1 秒，降低暴力猜密碼的速度 */
            continue;
        }
        run_session(acc);
    }
    return NULL;
}
