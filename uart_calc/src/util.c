/*
 * util.c - 共用的小工具函式
 */
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;                       /* 跳過前導空白 */
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';   /* 把尾端空白改成 '\0' */
    return s;
}

int parse_int(const char *v, int *out, int min, int max) {
    char *end;
    errno = 0;
    long n = strtol(v, &end, 10);
    /* end == v 代表一個數字都沒讀到；*end != '\0' 代表數字後面還有其他字元 */
    if (errno || end == v || *end != '\0' || n < min || n > max) return -1;
    *out = (int)n;
    return 0;
}
