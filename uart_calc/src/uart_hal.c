/*
 * uart_hal.c - UART 硬體抽象層的 Linux (termios) 實作
 */
#include "uart_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static int s_fd = -1;   /* UART 裝置的檔案描述子 */

/* 把數字形式的鮑率轉成 termios 使用的常數 (B115200 等)；不支援回傳 0 */
static speed_t baud_to_speed(int b) {
    switch (b) {
    case 1200: return B1200;     case 2400: return B2400;
    case 4800: return B4800;     case 9600: return B9600;
    case 19200: return B19200;   case 38400: return B38400;
    case 57600: return B57600;   case 115200: return B115200;
    case 230400: return B230400; case 460800: return B460800;
    case 921600: return B921600;
    default: return 0;
    }
}

int uart_hal_open(const char *device, int baud, int timeout_sec) {
    speed_t sp = baud_to_speed(baud);
    if (!sp) {
        fprintf(stderr, "[uart] unsupported baud rate %d\n", baud);
        return -1;
    }
    /* O_RDWR：可讀可寫；O_NOCTTY：不要讓這個序列埠變成本程式的控制終端機 */
    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        fprintf(stderr, "[uart] open %s: %s\n", device, strerror(errno));
        return -1;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {   /* 取得目前設定；失敗代表這不是終端機/序列埠裝置 */
        fprintf(stderr, "[uart] %s is not a serial device: %s\n", device, strerror(errno));
        close(fd);
        return -1;
    }
    /* raw 模式：關閉系統的行編輯、回顯、特殊字元處理，
     * 每個字元原封不動交給程式，由上層自己處理回顯與 Backspace */
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;                   /* 忽略數據機控制線、啟用接收 */
    tio.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);     /* 無硬體流控、1 停止位元、無同位元 */
    tio.c_cflag = (tio.c_cflag & ~CSIZE) | CS8;      /* 8 資料位元 */
    cfsetispeed(&tio, sp);                           /* 輸入鮑率 */
    cfsetospeed(&tio, sp);                           /* 輸出鮑率 */
    /* UART_TIMEOUT_SEC 套用在讀取: VMIN=0, VTIME 單位 0.1 秒
     * read() 最多等待 timeout_sec 秒, 逾時回傳 0, 讓上層藉此定期檢查踢除/關機
     * (類似 pySerial 的 timeout 參數對 readline() 的影響) */
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = (cc_t)(timeout_sec * 10);
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {   /* TCSANOW：設定立即生效 */
        fprintf(stderr, "[uart] tcsetattr: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);   /* 清掉緩衝區中開啟前殘留的資料 */
    s_fd = fd;
    return 0;
}

void uart_hal_close(void) {
    if (s_fd >= 0) close(s_fd);
    s_fd = -1;
}

int uart_hal_getc(unsigned char *c) {
    ssize_t n = read(s_fd, c, 1);   /* 一次讀一個字元，最多等 timeout_sec 秒 */
    if (n < 0) return (errno == EINTR) ? 0 : -1;   /* 被 signal 中斷視同逾時 */
    return n == 1 ? 1 : 0;                          /* n == 0 表示逾時 */
}

void uart_hal_write(const char *s, size_t n) {
    while (n > 0) {   /* write 可能一次寫不完，所以用迴圈 */
        ssize_t w = write(s_fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;   /* 被 signal 中斷，重試 */
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

void uart_hal_puts(const char *s) { uart_hal_write(s, strlen(s)); }

void uart_hal_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    uart_hal_puts(buf);
}
