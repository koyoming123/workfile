/*
 * strbuf.c - 可自動變長的字串緩衝區
 */
#include "strbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void sb_init(sb_t *s) {
    s->cap = 8192;
    s->len = 0;
    s->buf = malloc(s->cap);
    s->buf[0] = '\0';
}

void sb_free(sb_t *s) {
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

void sb_printf(sb_t *s, const char *fmt, ...) {
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);   /* n = 需要的長度 */
        va_end(ap);
        if (n < 0) return;
        if ((size_t)n < s->cap - s->len) { s->len += (size_t)n; return; }   /* 放得下，完成 */
        s->cap = (s->cap + (size_t)n) * 2;   /* 放不下：擴大容量後重來一次 */
        s->buf = realloc(s->buf, s->cap);
    }
}

void sb_html(sb_t *s, const char *t) {
    for (; *t; t++) {
        switch (*t) {
        case '<': sb_printf(s, "&lt;"); break;
        case '>': sb_printf(s, "&gt;"); break;
        case '&': sb_printf(s, "&amp;"); break;
        case '"': sb_printf(s, "&quot;"); break;
        case '\'': sb_printf(s, "&#39;"); break;
        default: sb_printf(s, "%c", *t);
        }
    }
}

void sb_json_str(sb_t *s, const char *t) {
    sb_printf(s, "\"");
    for (; *t; t++) {
        unsigned char c = (unsigned char)*t;
        if (c == '"' || c == '\\') sb_printf(s, "\\%c", c);
        else if (c < 0x20) sb_printf(s, "\\u%04x", c);
        else sb_printf(s, "%c", c);
    }
    sb_printf(s, "\"");
}
