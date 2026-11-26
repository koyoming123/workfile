/*
 * admin_cmd.h - 管理命令 (與原本 TCP 版的命令相同)
 *
 *   status          UART 登入者與各角色狀態
 *   lock <role>     鎖定該角色使用計算機
 *   unlock <role>   解鎖該角色使用計算機
 *   kick uart       強制中斷 UART session
 *   showlog [n]     顯示最後 n 行 log (預設 10)
 *   help            說明
 *   quit            登出
 *
 * 屬於應用層：只處理「命令文字 → 動作 → 輸出文字」，不知道命令是從哪裡來的。
 * 目前由網頁端 (web.c) 呼叫；如果之後要加回 nc 的 TCP 介面，
 * 只要寫一個新的介面模組呼叫 admin_exec() 即可，命令邏輯完全不用重寫。
 */
#ifndef ADMIN_CMD_H
#define ADMIN_CMD_H

#include "strbuf.h"

/* 管理命令在 log 中的 SOURCE 欄位 */
#define ADMIN_LOG_SOURCE "WEB"

/*
 * 執行一行管理命令，輸出附加到 out；回傳 1 表示 quit (登出)，其他回傳 0。
 * 每個命令不論成功失敗都會寫入 log。
 */
int admin_exec(const char *cmdline, sb_t *out);

#endif /* ADMIN_CMD_H */
