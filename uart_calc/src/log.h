/*
 * log.h - 日誌檔 log.txt
 *
 * 每筆事件寫成一行：
 *   YYYY-MM-DD HH:MM:SS | SOURCE | USER | ACTION | RESULT | DETAIL
 *   SOURCE : SYSTEM / UART / WEB
 *   RESULT : OK / FAIL / DENIED
 *
 * 本模組內部自帶互斥鎖，可以從任何執行緒呼叫。
 */
#ifndef LOG_H
#define LOG_H

/* 設定日誌檔路徑與最大行數，並計算檔案目前的行數。必須在其他 log 函式之前呼叫 */
void log_init(const char *path, int max_lines);

/*
 * 寫一筆日誌到 log.txt (同時印到螢幕方便觀察)。
 *   source : 來源，"SYSTEM" / "UART" / "WEB"
 *   user   : 使用者名稱
 *   action : 動作，如 "login"、"calc"、"lock_role"
 *   result : 結果，"OK" / "FAIL" / "DENIED"
 *   fmt... : 細節，用法同 printf，例如 log_event(..., "expr=%s result=%s", e, r)
 * user 與 detail 中的 '|' 與換行會被替換，確保每行格式固定。
 * 超過 max_lines 時自動刪掉最舊的紀錄。
 */
void log_event(const char *source, const char *user, const char *action,
               const char *result, const char *fmt, ...) __attribute__((format(printf, 5, 6)));

/* 取得 log.txt 最後 n 行，回傳 malloc 的字串，呼叫者要 free() */
char *log_tail(int n);

#endif /* LOG_H */
