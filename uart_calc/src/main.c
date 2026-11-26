/*
 * ============================================================================
 *  main.c - Raspberry Pi 4 UART 文字計算機 + 網頁管理端 (程式進入點)
 * ============================================================================
 *
 *  【系統功能】
 *   UART 端 : 使用者用 PuTTY (Serial) 連線，登入後進入文字計算機模式，
 *             支援 + - * / 與括號 ()。
 *   網頁端  : 管理者用瀏覽器開 http://<Pi_IP>:<TCP_PORT>/ ，可以查看狀態、
 *             鎖定 / 解鎖角色、踢除 UART 使用者、查看日誌。
 *   設定檔  : config.txt       日誌檔 : log.txt
 *
 *  【模組分層】上層只呼叫下層，下層不知道上層的存在
 *
 *   介面層   uart_session.c (UART 使用者介面)    web.c (網頁管理端)
 *   應用層   calc.c (算式求值)                   admin_cmd.c (管理命令)
 *   服務層   state.c (共享狀態)  log.c (日誌)    config.c (設定檔/帳號)
 *   硬體層   uart_hal.c (序列埠 termios)
 *   工具     role.c  util.c  strbuf.c
 *
 *  main() 只負責：讀設定 → 初始化各模組 → 建立兩條執行緒 → 等待關機訊號 → 收尾。
 * ============================================================================
 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"
#include "log.h"
#include "state.h"
#include "uart_hal.h"
#include "uart_session.h"
#include "web.h"

/* 設定值在整個程式執行期間都有效 (web 模組會保留指標) */
static config_t s_cfg;

/* signal handler：只做最簡單的事 (設旗標)，實際的關閉流程在 main() 中進行 */
static void on_signal(int sig) { (void)sig; app_stop(); }

int main(void) {
    /* 1. 讀取設定檔；有任何錯誤就不啟動 */
    if (config_load(CONFIG_FILE, &s_cfg) < 0) {
        fprintf(stderr, "config.txt invalid, abort.\n");
        return 1;
    }
    state_init(s_cfg.allow);   /* 依 ALLOW_xxx 設定各角色初始的計算機權限 */
    log_init(s_cfg.log_file, s_cfg.max_log_lines);

    /* 2. 設定訊號處理：Ctrl+C (SIGINT) 與 kill (SIGTERM) 都進入正常關閉流程 */
    struct sigaction sa = {0};
    sa.sa_handler = on_signal; /* 不設 SA_RESTART, 讓阻塞呼叫被中斷 */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);   /* 瀏覽器提早斷線時，寫入 socket 不會讓程式被終止 */

    /* 3. 開啟 UART 與網頁監聽 socket，失敗就記錄並結束 */
    if (uart_hal_open(s_cfg.uart_device, s_cfg.uart_baud, s_cfg.uart_timeout_sec) < 0) {
        log_event("SYSTEM", "-", "startup", "FAIL", "cannot open uart %s", s_cfg.uart_device);
        return 1;
    }
    if (web_init(&s_cfg) < 0) {
        log_event("SYSTEM", "-", "startup", "FAIL", "cannot listen on port %d", s_cfg.tcp_port);
        uart_hal_close();
        return 1;
    }

    log_event("SYSTEM", "-", "startup", "OK",
              "service started uart=%s baud=%d web_port=%d timeout=%ds",
              s_cfg.uart_device, s_cfg.uart_baud, s_cfg.tcp_port, s_cfg.uart_timeout_sec);

    /* 4. 建立兩條執行緒，UART 與網頁同時運作 */
    pthread_t tu, tw;
    pthread_create(&tu, NULL, uart_session_thread, NULL);
    pthread_create(&tw, NULL, web_thread, NULL);

    /* 5. 主執行緒在這裡睡覺，直到收到訊號 (pause() 被訊號喚醒，app_running() 變 0) */
    while (app_running()) pause();

    /* 6. 等兩條執行緒結束 (它們最多在 1 秒 / UART_TIMEOUT_SEC 內會發現要關機)，
     *    再關閉資源並記錄 shutdown */
    pthread_join(tw, NULL);
    pthread_join(tu, NULL);
    web_close();
    uart_hal_close();
    log_event("SYSTEM", "-", "shutdown", "OK", "service stopped");
    return 0;
}
