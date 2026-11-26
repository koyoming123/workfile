/*
 * calc.c - 計算機 (遞迴下降解析)
 *
 * 用三層函式處理運算子優先順序 (文法如下，越下面優先權越高)：
 *   expr   := term   (('+'|'-') term)*        ← 加減，最低優先
 *   term   := factor (('*'|'/') factor)*      ← 乘除
 *   factor := ('+'|'-') factor | number | '(' expr ')'   ← 正負號、數字、括號
 *
 * 例：2+3*4
 *   p_expr 先呼叫 p_term 得到 2，看到 '+'，再呼叫 p_term；
 *   第二個 p_term 會把 3*4 先算成 12 才回傳，所以結果是 2+12=14。
 *   這樣「先乘除後加減」自然就成立了。
 */
#include "calc.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;    /* 目前解析到的位置 */
    const char *err;  /* 錯誤訊息；NULL 表示目前沒有錯誤 */
    int depth;        /* 遞迴深度，防止 "((((((..." 造成堆疊溢位 */
} parser_t;

static double p_expr(parser_t *ps);   /* 前置宣告：p_factor 處理括號時要呼叫它 */

/* 跳過空白與 Tab，讓 "2 + 3" 也能正確解析 */
static void p_ws(parser_t *ps) { while (*ps->p == ' ' || *ps->p == '\t') ps->p++; }

/* 解析 factor：正負號、數字或括號運算式 */
static double p_factor(parser_t *ps) {
    if (ps->err) return 0;   /* 已經有錯就不用繼續 */
    if (++ps->depth > 200) { ps->err = "expression too deep"; return 0; }
    p_ws(ps);
    double v = 0;
    char c = *ps->p;
    if (c == '+' || c == '-') {
        /* 一元正負號，例如 -3、-(2+1)、--5 */
        ps->p++;
        v = p_factor(ps);
        v = (c == '-') ? -v : v;
    } else if (c == '(') {
        /* 括號：內部是一個完整的 expr，最後必須有 ')' */
        ps->p++;
        v = p_expr(ps);
        p_ws(ps);
        if (!ps->err) {
            if (*ps->p == ')') ps->p++;
            else ps->err = "missing ')'";
        }
    } else if (isdigit((unsigned char)c) || c == '.') {
        /* 數字：由數字與最多一個小數點組成，例如 12、3.5、.5 */
        const char *s = ps->p;
        int digits = 0, dots = 0;
        while (isdigit((unsigned char)*ps->p) || *ps->p == '.') {
            if (*ps->p == '.') dots++; else digits++;
            ps->p++;
        }
        if (!digits || dots > 1) { ps->err = "invalid number"; return 0; }   /* 如 "." 或 "1.2.3" */
        /* 複製到暫存陣列再用 strtod 轉換。
         * 不直接對原字串用 strtod，是因為 strtod 會接受 "inf"、"0x1F"、"1e5" 等格式，
         * 我們只想接受單純的十進位數字。 */
        char num[64];
        size_t n = (size_t)(ps->p - s);
        if (n >= sizeof num) { ps->err = "number too long"; return 0; }
        memcpy(num, s, n);
        num[n] = '\0';
        v = strtod(num, NULL);
    } else if (c == '\0') {
        ps->err = "unexpected end of expression";   /* 例如 "2+" */
    } else {
        ps->err = "invalid character";              /* 例如 "2+a" 或 "2+*3" */
    }
    ps->depth--;
    return v;
}

/* 解析 term：一連串以 * 或 / 連接的 factor (由左到右計算) */
static double p_term(parser_t *ps) {
    double v = p_factor(ps);
    for (;;) {
        if (ps->err) return 0;
        p_ws(ps);
        char op = *ps->p;
        if (op != '*' && op != '/') return v;   /* 不是乘除就結束，交回給上一層 */
        ps->p++;
        double r = p_factor(ps);
        if (ps->err) return 0;
        if (op == '*') v *= r;
        else if (r == 0) { ps->err = "division by zero"; return 0; }   /* 除以 0 視為錯誤 */
        else v /= r;
    }
}

/* 解析 expr：一連串以 + 或 - 連接的 term (由左到右計算) */
static double p_expr(parser_t *ps) {
    double v = p_term(ps);
    for (;;) {
        if (ps->err) return 0;
        p_ws(ps);
        char op = *ps->p;
        if (op != '+' && op != '-') return v;   /* 不是加減就結束 */
        ps->p++;
        double r = p_term(ps);
        v = (op == '+') ? v + r : v - r;
    }
}

int calc_eval(const char *expr, char *out, size_t n) {
    parser_t ps = {expr, NULL, 0};
    double v = p_expr(&ps);
    p_ws(&ps);
    /* 解析完整個運算式後，後面不應該還有剩下的字元 */
    if (!ps.err && *ps.p == ')') ps.err = "unmatched ')'";     /* 例如 "2+3)" */
    if (!ps.err && *ps.p != '\0') ps.err = "invalid character";
    if (!ps.err && !isfinite(v)) ps.err = "overflow";          /* 數值太大變成無限大 */
    if (ps.err) {
        snprintf(out, n, "%s", ps.err);
        return -1;
    }
    if (v == 0) v = 0; /* 避免顯示 -0 (浮點數的 -0 == 0 為真，重新指定為正 0) */
    if (fabs(v) < 1e15 && v == floor(v)) snprintf(out, n, "%.0f", v);   /* 整數 */
    else snprintf(out, n, "%.10g", v);                                  /* 小數 */
    return 0;
}
