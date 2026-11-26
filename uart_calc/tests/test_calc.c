/*
 * test_calc.c - calc 模組的單元測試
 *
 * calc 是純函式模組，不需要 UART 也不需要樹莓派，在任何 Linux/PC 上都能執行：
 *     make test
 */
#include <stdio.h>
#include <string.h>

#include "calc.h"

static int s_fail;   /* 失敗的測試數 */
static int s_total;  /* 總測試數 */

/* 預期計算成功，結果為 want */
static void expect_ok(const char *expr, const char *want) {
    char out[128];
    s_total++;
    int r = calc_eval(expr, out, sizeof out);
    if (r != 0 || strcmp(out, want) != 0) {
        printf("FAIL  calc(\"%s\") = %s \"%s\", want OK \"%s\"\n", expr, r ? "ERR" : "OK", out, want);
        s_fail++;
    }
}

/* 預期計算失敗，錯誤訊息為 want */
static void expect_err(const char *expr, const char *want) {
    char out[128];
    s_total++;
    int r = calc_eval(expr, out, sizeof out);
    if (r == 0 || strcmp(out, want) != 0) {
        printf("FAIL  calc(\"%s\") = %s \"%s\", want ERR \"%s\"\n", expr, r ? "ERR" : "OK", out, want);
        s_fail++;
    }
}

int main(void) {
    /* 題目範例 */
    expect_ok("2+3*4", "14");
    expect_ok("(10-2)/4", "2");
    expect_ok("1+1", "2");

    /* 運算子優先順序與結合方向 */
    expect_ok("2*3+4", "10");
    expect_ok("10-4-3", "3");      /* 由左到右：(10-4)-3 */
    expect_ok("100/10/5", "2");    /* 由左到右：(100/10)/5 */
    expect_ok("2*(3+4)", "14");
    expect_ok("((1+2)*(3+4))", "21");

    /* 正負號、小數、空白 */
    expect_ok("-3*(2+1.5)", "-10.5");
    expect_ok("--5", "5");
    expect_ok("-(2+3)", "-5");
    expect_ok("7/2", "3.5");
    expect_ok(".5+.5", "1");
    expect_ok("1/3", "0.3333333333");
    expect_ok("  2 +\t3  ", "5");
    expect_ok("0-0", "0");         /* 不可顯示 -0 */
    expect_ok("-0", "0");

    /* 錯誤處理 */
    expect_err("1/0", "division by zero");
    expect_err("5/(2-2)", "division by zero");
    expect_err("2+*3", "invalid character");
    expect_err("2+a", "invalid character");
    expect_err("2+", "unexpected end of expression");
    expect_err("", "unexpected end of expression");
    expect_err("(1+2", "missing ')'");
    expect_err("1+2)", "unmatched ')'");
    expect_err("1.2.3", "invalid number");
    expect_err(".", "invalid number");
    expect_err("1e5", "invalid character");   /* 不接受科學記號 */
    expect_err("0x1F", "invalid character");  /* 不接受十六進位 */

    /* 遞迴深度上限：防止堆疊溢位 */
    char deep[512];
    memset(deep, '(', 300);
    strcpy(deep + 300, "1");
    expect_err(deep, "expression too deep");

    printf("calc: %d/%d passed\n", s_total - s_fail, s_total);
    return s_fail ? 1 : 0;
}
