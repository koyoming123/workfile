/*
 * uart_hal.h - UART 硬體抽象層 (HAL, Hardware Abstraction Layer)
 *
 * 整個程式只有這個模組直接使用 Linux 的 termios 與 read()/write() 操作序列埠。
 * 上層 (uart_session) 只透過下面的函式收發字元，不知道底下是什麼硬體。
 * 要移植到 RTOS 或 bare-metal MCU 時，只要用同樣的介面重寫 uart_hal.c，
 * 例如用 HAL_UART_Receive() 實作 uart_hal_getc()，上層程式碼完全不用改。
 */
#ifndef UART_HAL_H
#define UART_HAL_H

#include <stddef.h>

/*
 * 開啟並設定 UART：8 資料位元、無同位元、1 停止位元 (8N1)、無流量控制、raw 模式。
 * timeout_sec 套用在每次讀取：uart_hal_getc() 最多等這麼多秒。
 * 成功回傳 0，失敗回傳 -1 (原因印到 stderr)。
 */
int uart_hal_open(const char *device, int baud, int timeout_sec);
void uart_hal_close(void);

/*
 * 讀一個字元，最多等待 timeout_sec 秒。
 * 回傳 1：讀到字元，存入 *c
 * 回傳 0：逾時或被 signal 中斷，沒有讀到字元 (呼叫者可趁機檢查其他事件)
 * 回傳 -1：讀取錯誤
 */
int uart_hal_getc(unsigned char *c);

/* 把 n 個位元組完整寫到 UART */
void uart_hal_write(const char *s, size_t n);
/* 輸出字串到 UART */
void uart_hal_puts(const char *s);
/* printf 風格輸出到 UART，例如 uart_hal_printf("%s\r\n", result) */
void uart_hal_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* UART_HAL_H */
