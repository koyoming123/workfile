/*
 * config.h - 設定檔 config.txt 與 UART 帳號表
 *
 * 格式：一行一個 key=value，空行與 # 開頭的行會被忽略。
 * 必要欄位缺任何一個、數值不合法、或 LOG_FILE 不是 log.txt，config_load() 都會失敗。
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "role.h"

#define CONFIG_FILE "config.txt"     /* 設定檔名稱 (題目規定固定) */
#define REQUIRED_LOG_FILE "log.txt"  /* LOG_FILE 必須是這個名稱 */

typedef struct {
    int tcp_port;              /* 網頁管理端監聽的 port (TCP_PORT) */
    char uart_device[256];     /* UART 裝置路徑，如 /dev/serial0 */
    int uart_baud;             /* UART 鮑率，如 115200 */
    char log_file[256];        /* 日誌檔名，必須是 log.txt */
    int max_log_lines;         /* log.txt 最多保留幾行 */
    int allow[ROLE_COUNT];     /* 啟動時各角色是否允許使用計算機 (ALLOW_xxx) */
    char admin_password[128];  /* 網頁管理端登入密碼 */
    int uart_timeout_sec;      /* UART 每次 read() 的逾時秒數 */
} config_t;

/* 一個 UART 登入帳號 */
typedef struct {
    char name[32];  /* 帳號 */
    char pass[64];  /* 密碼 */
    int role;       /* 角色 (ROLE_GUEST / ROLE_OPERATOR / ROLE_ADMIN) */
} account_t;

/*
 * 讀取並驗證設定檔，結果寫入 *cfg，同時載入 USERS 帳號表
 * (未設定 USERS 時使用預設帳號)。
 * 成功回傳 0；任何錯誤回傳 -1，錯誤原因會印到 stderr。
 */
int config_load(const char *path, config_t *cfg);

/* 依帳號密碼查詢帳號表；正確回傳該帳號，錯誤回傳 NULL */
const account_t *config_find_account(const char *name, const char *pass);

#endif /* CONFIG_H */
