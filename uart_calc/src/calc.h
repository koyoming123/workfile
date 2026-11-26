/*
 * calc.h - 四則運算算式求值
 *
 * 純函式模組：不碰 UART、不寫 log、沒有全域狀態，
 * 所以可以在 PC 上直接做單元測試 (見 tests/test_calc.c)。
 */
#ifndef CALC_H
#define CALC_H

#include <stddef.h>

/*
 * 計算算式字串 expr，支援 + - * /、括號、小數、正負號，例如 "-3*(2+1.5)"。
 * 成功回傳 0，結果字串放入 out：整數直接顯示 ("14")，非整數最多 10 位有效數字 ("3.5")。
 * 失敗回傳 -1，out 為錯誤訊息，例如 "division by zero"、"invalid character"。
 */
int calc_eval(const char *expr, char *out, size_t n);

#endif /* CALC_H */
