/*
 * strbuf.h - 可自動變長的字串緩衝區 (string builder)
 *
 * 用來組合長度不固定的輸出，例如管理命令的結果、HTML 頁面、JSON 回應。
 * 用法：
 *     sb_t s;
 *     sb_init(&s);
 *     sb_printf(&s, "x=%d\n", 1);
 *     ... 使用 s.buf / s.len ...
 *     sb_free(&s);
 */
#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>

typedef struct {
    char *buf;       /* 字串內容 (永遠以 '\0' 結尾) */
    size_t len, cap; /* 目前長度、已配置容量 */
} sb_t;

void sb_init(sb_t *s);
void sb_free(sb_t *s);

/* 以 printf 格式附加內容；空間不夠時自動擴大 */
void sb_printf(sb_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/*
 * 附加 HTML 跳脫後的文字 (< > & " ' 轉成 &lt; 等實體)。
 * 內容若可能含有使用者輸入，放進網頁前一定要經過這個函式，防止 XSS 攻擊。
 */
void sb_html(sb_t *s, const char *t);

/* 附加一個 JSON 字串 (含前後雙引號，並跳脫 " \ 與控制字元) */
void sb_json_str(sb_t *s, const char *t);

#endif /* STRBUF_H */
