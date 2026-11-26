/*
 * util.h - 共用的小工具函式 (字串處理、數字解析)
 *
 * 不依賴任何其他模組，config、uart_session、admin_cmd、web 都會用到。
 */
#ifndef UTIL_H
#define UTIL_H

/* 去掉字串前後的空白字元 (會直接修改原字串)，回傳去掉前導空白後的位置 */
char *trim(char *s);

/*
 * 把字串 v 轉成整數，並檢查範圍是否在 [min, max]。
 * 成功：結果寫入 *out，回傳 0。
 * 失敗 (不是數字、後面有多餘字元、超出範圍)：回傳 -1，*out 不變。
 */
int parse_int(const char *v, int *out, int min, int max);

#endif /* UTIL_H */
