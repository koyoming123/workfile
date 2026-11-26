/*
 * uart_session.h - UART 端的使用者介面 (登入流程 + 計算機模式)
 *
 * 屬於介面層：使用 uart_hal 收發字元、config 驗證帳號、
 * state 檢查權限與踢除、calc 計算、log 記錄事件。
 */
#ifndef UART_SESSION_H
#define UART_SESSION_H

/*
 * UART 執行緒主函式 (給 pthread_create 使用，arg 不使用)。
 * 迴圈：Login: → Password: → 驗證 → 計算機模式 → 回到 Login:
 * 直到 app_running() 變成 0 才返回。
 */
void *uart_session_thread(void *arg);

#endif /* UART_SESSION_H */
