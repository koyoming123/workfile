/*
 * admin_cmd.c - 管理命令的實作
 */
#include "admin_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "role.h"
#include "state.h"
#include "util.h"

/* 產生 status 命令的輸出文字，格式依照題目範例 */
static void status_text(sb_t *s) {
    state_snapshot_t st;
    state_snapshot(&st);
    if (st.uart_active)
        sb_printf(s, "UART user=%s role=%s state=active\n", st.uart_user, ROLE_NAME[st.uart_role]);
    else
        sb_printf(s, "UART user=- role=- state=idle\n");
    for (int i = 0; i < ROLE_COUNT; i++)
        sb_printf(s, "%s=%s\n", ROLE_NAME[i], st.role_enabled[i] ? "enabled" : "disabled");
}

/*
 * 命令最多三段：cmd arg extra，例如 "lock operator" → cmd="lock", arg="operator"。
 * extra 不為 NULL 表示參數太多，視為格式錯誤。
 */
int admin_exec(const char *cmdline, sb_t *out) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", cmdline);   /* 複製一份給 strtok_r 切割 */
    char *save = NULL;
    char *cmd = strtok_r(buf, " \t", &save);     /* 第一段：命令 */
    char *arg = strtok_r(NULL, " \t", &save);    /* 第二段：參數 (可能沒有) */
    char *extra = strtok_r(NULL, " \t", &save);  /* 第三段：多餘參數 (有就是格式錯誤) */

    if (!cmd) { sb_printf(out, "ERROR empty command\n"); return 0; }

    if (strcmp(cmd, "help") == 0 && !arg) {
        /* help：列出所有命令 */
        sb_printf(out,
                  "status          show UART session and role states\n"
                  "lock <role>     disable calculator for role (guest|operator|admin)\n"
                  "unlock <role>   enable calculator for role\n"
                  "kick uart       terminate current UART session\n"
                  "showlog [n]     show last n log lines (default 10)\n"
                  "help            show this help\n"
                  "quit            logout from web console\n");
        log_event(ADMIN_LOG_SOURCE, "admin", "help", "OK", "-");
    } else if (strcmp(cmd, "status") == 0 && !arg) {
        /* status：UART 登入者與各角色狀態 */
        status_text(out);
        log_event(ADMIN_LOG_SOURCE, "admin", "status", "OK", "-");
    } else if ((strcmp(cmd, "lock") == 0 || strcmp(cmd, "unlock") == 0) && !extra) {
        /* lock / unlock <role>：修改角色的計算機使用權限 */
        int lock = (cmd[0] == 'l');   /* 'l'ock 或 'u'nlock */
        const char *action = lock ? "lock_role" : "unlock_role";
        int r = arg ? role_from_name(arg) : -1;
        if (r < 0) {
            sb_printf(out, "ERROR usage: %s guest|operator|admin\n", cmd);
            log_event(ADMIN_LOG_SOURCE, "admin", action, "FAIL", "invalid role=%s", arg ? arg : "-");
        } else {
            state_set_role_enabled(r, !lock);   /* lock → 0 (停用)，unlock → 1 (啟用) */
            sb_printf(out, "OK\n");
            log_event(ADMIN_LOG_SOURCE, "admin", action, "OK", "role=%s", ROLE_NAME[r]);
        }
    } else if (strcmp(cmd, "kick") == 0 && arg && strcmp(arg, "uart") == 0 && !extra) {
        /* kick uart：設定踢除旗標。
         * 這裡不直接操作 UART，而是由 UART 執行緒在下一次讀取逾時
         * (最多 UART_TIMEOUT_SEC 秒) 時看到旗標，自行結束 session。 */
        char user[32];
        if (state_request_kick(user, sizeof user)) {
            sb_printf(out, "OK\n");
            log_event(ADMIN_LOG_SOURCE, "admin", "kick_uart", "OK", "user=%s", user);
        } else {
            sb_printf(out, "ERROR no active UART session\n");
            log_event(ADMIN_LOG_SOURCE, "admin", "kick_uart", "FAIL", "no active uart session");
        }
    } else if (strcmp(cmd, "showlog") == 0 && !extra) {
        /* showlog [n]：顯示最後 n 行 log，預設 10 行 */
        int n = 10;
        if (arg && (parse_int(arg, &n, 1, 1000) < 0)) {
            sb_printf(out, "ERROR usage: showlog [1-1000]\n");
            log_event(ADMIN_LOG_SOURCE, "admin", "showlog", "FAIL", "invalid count=%s", arg);
            return 0;
        }
        char *t = log_tail(n);   /* 先讀 log 再寫入這次 showlog 的紀錄 */
        sb_printf(out, "%s", t);
        free(t);
        log_event(ADMIN_LOG_SOURCE, "admin", "showlog", "OK", "last %d lines", n);
    } else if (strcmp(cmd, "quit") == 0 && !arg) {
        /* quit：登出 */
        sb_printf(out, "bye\n");
        log_event(ADMIN_LOG_SOURCE, "admin", "logout", "OK", "-");
        return 1;
    } else {
        /* 不認得的命令或參數格式錯誤 */
        sb_printf(out, "ERROR unknown command (type help)\n");
        log_event(ADMIN_LOG_SOURCE, "admin", "command", "FAIL", "unknown cmd=%s", cmdline);
    }
    return 0;
}
