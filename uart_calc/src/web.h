/*
 * web.h - 網頁管理端 (極簡 HTTP 伺服器)
 *
 * 不依賴任何外部函式庫，自己實作：
 *   GET  /            未登入 → 登入頁；已登入 → 管理主頁
 *   POST /login       送出管理密碼
 *   POST /cmd         執行管理命令 (交給 admin_cmd 模組)
 *   GET  /api/status  回傳 JSON 狀態，給頁面上的 JavaScript 每 2 秒自動更新
 * 登入狀態用 Cookie (sid=隨機字串) 識別。
 */
#ifndef WEB_H
#define WEB_H

#include "config.h"

/*
 * 在 cfg->tcp_port 建立監聽 socket。cfg 的內容在程式執行期間必須保持有效
 * (會用到管理密碼與頁尾顯示的設定值)。
 * 成功回傳 0，失敗回傳 -1。
 */
int web_init(const config_t *cfg);
void web_close(void);

/* 網頁執行緒主函式 (給 pthread_create 使用，arg 不使用)，直到 app_running() 變成 0 才返回 */
void *web_thread(void *arg);

#endif /* WEB_H */
