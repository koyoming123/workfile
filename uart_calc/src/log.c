/*
 * log.c - 日誌檔 log.txt 的寫入、修剪與讀取
 *
 * UART 與網頁兩條執行緒都會寫 log，所以用 s_mtx 保護，確保一次只有一條在寫。
 */
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static char s_path[256];   /* 日誌檔路徑 */
static int s_max_lines;    /* 最多保留幾行 */
static int s_lines;        /* 目前 log.txt 的行數 (用來判斷是否超過上限) */

/*
 * 清理要寫進 log 的欄位：欄位中不可出現 '|' 與換行，以免破壞每行固定的格式。
 *   '|'            → '/'
 *   換行、Tab      → 空白
 *   其他控制字元   → '?'
 *   空字串或 NULL  → "-"
 */
static void sanitize(char *dst, size_t n, const char *src) {
    size_t j = 0;
    if (!src || !*src) src = "-";
    for (; *src && j + 1 < n; src++) {   /* j + 1 < n：保留結尾 '\0' 的空間 */
        unsigned char c = (unsigned char)*src;
        if (c == '|') c = '/';
        else if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        else if (c < 0x20 || c == 0x7f) c = '?';
        dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

/*
 * 把整個檔案讀進記憶體 (malloc 配置)，結尾補 '\0' 方便當字串處理。
 * 檔案長度寫入 *len_out (可傳 NULL)。檔案不存在回傳 NULL。
 * 呼叫者用完要 free()。
 */
static char *read_whole_file(const char *path, long *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);   /* 移到檔尾取得檔案大小 */
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);   /* 回到檔頭開始讀 */
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    len = (long)fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    if (len_out) *len_out = len;
    return buf;
}

/* 計算字串中有幾個換行 (= 幾行) */
static int count_lines(const char *buf) {
    int n = 0;
    for (; *buf; buf++) if (*buf == '\n') n++;
    return n;
}

/*
 * 超過上限時只保留最後 s_max_lines 行 (呼叫者需持有 s_mtx 鎖)。
 * 作法：讀入整個檔案 → 跳過前面多出來的行 → 寫到暫存檔 → rename 覆蓋原檔。
 * 用「先寫暫存檔再 rename」的方式，即使寫到一半斷電，原本的 log.txt 也不會壞掉。
 */
static void trim_locked(void) {
    long len;
    char *buf = read_whole_file(s_path, &len);
    if (!buf) return;
    int total = count_lines(buf);
    int skip = total - s_max_lines;   /* 要丟掉的舊行數 */
    char *p = buf;
    while (skip > 0 && *p) { if (*p++ == '\n') skip--; }   /* p 移到要保留的第一行開頭 */

    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", s_path);   /* 暫存檔：log.txt.tmp */
    FILE *f = fopen(tmp, "w");
    if (f) {
        fputs(p, f);
        fclose(f);
        rename(tmp, s_path);   /* 用暫存檔取代原檔 */
        s_lines = count_lines(p);
    }
    free(buf);
}

void log_init(const char *path, int max_lines) {
    snprintf(s_path, sizeof s_path, "%s", path);
    s_max_lines = max_lines;
    char *buf = read_whole_file(s_path, NULL);   /* 計算 log.txt 現有行數 (不存在就是 0) */
    s_lines = buf ? count_lines(buf) : 0;
    free(buf);
}

void log_event(const char *source, const char *user, const char *action,
               const char *result, const char *fmt, ...) {
    /* 1. 先把可變參數組成 detail 字串 */
    char detail[512], d2[512], u2[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    /* 2. 清理 detail 與 user，避免含有 '|' 或換行破壞格式 (這兩個可能來自使用者輸入) */
    sanitize(d2, sizeof d2, detail);
    sanitize(u2, sizeof u2, user);

    /* 3. 產生時間字串 YYYY-MM-DD HH:MM:SS (localtime_r 是執行緒安全版本) */
    char ts[32];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    /* 4. 上鎖後附加寫入檔案，每次都 fopen/fclose，確保內容立即寫到磁碟 */
    pthread_mutex_lock(&s_mtx);
    FILE *f = fopen(s_path, "a");
    if (f) {
        fprintf(f, "%s | %s | %s | %s | %s | %s\n", ts, source, u2, action, result, d2);
        fclose(f);
        if (++s_lines > s_max_lines) trim_locked();   /* 超過上限就修剪 */
    }
    printf("%s | %s | %s | %s | %s | %s\n", ts, source, u2, action, result, d2);
    fflush(stdout);
    pthread_mutex_unlock(&s_mtx);
}

/* 作法：讀入整個檔案，從尾端往前數換行，數到第 n 個換行就停 */
char *log_tail(int n) {
    pthread_mutex_lock(&s_mtx);   /* 讀檔時也上鎖，避免讀到寫一半的內容 */
    long len = 0;
    char *buf = read_whole_file(s_path, &len);
    pthread_mutex_unlock(&s_mtx);
    if (!buf) return strdup("");
    char *p = buf + len;                   /* 從檔尾開始 */
    if (p > buf && p[-1] == '\n') p--;     /* 忽略最後一行結尾的換行 */
    int cnt = 0;
    while (p > buf) {
        if (p[-1] == '\n' && ++cnt == n) break;   /* 前一個字元是換行 → 這裡是一行的開頭 */
        p--;
    }
    char *out = strdup(p);
    free(buf);
    return out;
}
