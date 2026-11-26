/*
 * test_config.c - config 模組的單元測試
 *
 * 每個測試寫一個暫存的設定檔，檢查 config_load() 是否正確接受或拒絕。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

static int s_fail;
static int s_total;
static char s_path[64];   /* 暫存設定檔路徑 */

/* 完整且合法的設定內容 */
static const char *const GOOD =
    "TCP_PORT=5000\n"
    "UART_DEVICE=/dev/serial0\n"
    "UART_BAUD=115200\n"
    "LOG_FILE=log.txt\n"
    "MAX_LOG_LINES=1000\n"
    "ALLOW_GUEST=1\n"
    "ALLOW_OPERATOR=1\n"
    "ALLOW_ADMIN=0\n"
    "ADMIN_PASSWORD=admin123\n"
    "UART_TIMEOUT_SEC=1\n";

/* 把 text 寫進暫存檔後呼叫 config_load() */
static int load_text(const char *text, config_t *cfg) {
    FILE *f = fopen(s_path, "w");
    fputs(text, f);
    fclose(f);
    return config_load(s_path, cfg);
}

static void check(int cond, const char *name) {
    s_total++;
    if (!cond) {
        printf("FAIL  %s\n", name);
        s_fail++;
    }
}

/* 從 GOOD 中刪掉 key 開頭的那一行 */
static void without_key(const char *key, char *out, size_t n) {
    out[0] = '\0';
    const char *p = GOOD;
    while (*p) {
        const char *e = strchr(p, '\n') + 1;
        if (strncmp(p, key, strlen(key)) != 0) strncat(out, p, (size_t)(e - p) < n - strlen(out) - 1 ? (size_t)(e - p) : n - strlen(out) - 1);
        p = e;
    }
}

int main(void) {
    snprintf(s_path, sizeof s_path, "test_config_%d.txt", (int)getpid());
    config_t cfg;
    char buf[2048];

    /* 合法設定：所有欄位都正確讀取 */
    check(load_text(GOOD, &cfg) == 0, "good config loads");
    check(cfg.tcp_port == 5000, "TCP_PORT");
    check(strcmp(cfg.uart_device, "/dev/serial0") == 0, "UART_DEVICE");
    check(cfg.uart_baud == 115200, "UART_BAUD");
    check(cfg.max_log_lines == 1000, "MAX_LOG_LINES");
    check(cfg.allow[ROLE_GUEST] == 1 && cfg.allow[ROLE_OPERATOR] == 1 && cfg.allow[ROLE_ADMIN] == 0, "ALLOW_xxx");
    check(strcmp(cfg.admin_password, "admin123") == 0, "ADMIN_PASSWORD");
    check(cfg.uart_timeout_sec == 1, "UART_TIMEOUT_SEC");

    /* 未設定 USERS：使用預設帳號 */
    const account_t *a = config_find_account("alice", "alice123");
    check(a && a->role == ROLE_OPERATOR, "default account alice is operator");
    check(config_find_account("alice", "wrong") == NULL, "wrong password rejected");
    check(config_find_account("nobody", "x") == NULL, "unknown user rejected");

    /* 自訂 USERS */
    snprintf(buf, sizeof buf, "%sUSERS=tom:t1:admin, amy:a1:guest\n", GOOD);
    check(load_text(buf, &cfg) == 0, "custom USERS loads");
    a = config_find_account("tom", "t1");
    check(a && a->role == ROLE_ADMIN, "custom account tom is admin");
    a = config_find_account("amy", "a1");
    check(a && a->role == ROLE_GUEST, "custom account amy is guest");
    check(config_find_account("alice", "alice123") == NULL, "default accounts replaced by USERS");

    /* 註解與空行會被忽略 */
    snprintf(buf, sizeof buf, "# comment\n\n%s", GOOD);
    check(load_text(buf, &cfg) == 0, "comments and blank lines ignored");

    /* 缺少任一必要欄位都要失敗 */
    const char *keys[] = {"TCP_PORT=", "UART_DEVICE=", "UART_BAUD=", "LOG_FILE=", "MAX_LOG_LINES=",
                          "ALLOW_GUEST=", "ALLOW_OPERATOR=", "ALLOW_ADMIN=", "ADMIN_PASSWORD=",
                          "UART_TIMEOUT_SEC="};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        char name[64];
        without_key(keys[i], buf, sizeof buf);
        snprintf(name, sizeof name, "missing %.*s rejected", (int)strlen(keys[i]) - 1, keys[i]);
        check(load_text(buf, &cfg) < 0, name);
    }

    /* 不合法的值都要失敗 */
    const char *bad[][2] = {
        {"LOG_FILE=", "LOG_FILE=other.txt\n"},
        {"TCP_PORT=", "TCP_PORT=70000\n"},
        {"TCP_PORT=", "TCP_PORT=abc\n"},
        {"ALLOW_GUEST=", "ALLOW_GUEST=2\n"},
        {"UART_TIMEOUT_SEC=", "UART_TIMEOUT_SEC=0\n"},
        {"UART_TIMEOUT_SEC=", "UART_TIMEOUT_SEC=30\n"},
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char name[96];
        without_key(bad[i][0], buf, sizeof buf);
        strncat(buf, bad[i][1], sizeof buf - strlen(buf) - 1);
        snprintf(name, sizeof name, "invalid %.*s rejected", (int)strlen(bad[i][1]) - 1, bad[i][1]);
        check(load_text(buf, &cfg) < 0, name);
    }
    snprintf(buf, sizeof buf, "%sUSERS=bad_entry\n", GOOD);
    check(load_text(buf, &cfg) < 0, "malformed USERS rejected");
    snprintf(buf, sizeof buf, "%sUSERS=x:y:superuser\n", GOOD);
    check(load_text(buf, &cfg) < 0, "unknown role in USERS rejected");
    snprintf(buf, sizeof buf, "%sNO_EQUALS_SIGN\n", GOOD);
    check(load_text(buf, &cfg) < 0, "line without '=' rejected");

    /* 檔案不存在 */
    unlink(s_path);
    check(config_load(s_path, &cfg) < 0, "missing file rejected");

    printf("config: %d/%d passed\n", s_total - s_fail, s_total);
    return s_fail ? 1 : 0;
}
