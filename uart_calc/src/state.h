/*
 * state.h - UART 執行緒與網頁執行緒的共享狀態
 *
 * 變數全部藏在 state.c 裡 (static)，外部只能透過下面的函式存取，
 * 鎖也在函式內部處理，呼叫者不需要自己上鎖，也不可能忘記上鎖。
 *
 * 需要同時讀多個欄位時 (例如 status 命令)，用 state_snapshot() 一次複製一份，
 * 保證讀到的是同一個時間點的一致資料。
 */
#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <time.h>

#include "role.h"

/* 某一時間點的共享狀態副本 */
typedef struct {
    int role_enabled[ROLE_COUNT];  /* 各角色目前是否允許使用計算機 */
    int uart_active;               /* UART 是否有人登入中 */
    char uart_user[32];            /* UART 目前登入的帳號 (未登入時為空字串) */
    int uart_role;                 /* UART 目前登入者的角色 */
    time_t uart_login_time;        /* UART 登入時間 */
} state_snapshot_t;

/* 依 config.txt 的 ALLOW_xxx 設定各角色初始的計算機權限 */
void state_init(const int allow[ROLE_COUNT]);

/* 取得目前狀態的一致副本 */
void state_snapshot(state_snapshot_t *out);

/* ---- 角色權限 ---- */
int state_role_enabled(int role);
void state_set_role_enabled(int role, int enabled);

/* ---- UART session (由 UART 執行緒呼叫) ---- */
/* 開始一個 UART session：記錄登入者，並清除殘留的踢除請求 */
void state_session_begin(const char *user, int role);
/* 結束 UART session：清除登入者與踢除請求 */
void state_session_end(void);
/* 網頁端是否要求踢除目前的 UART session */
int state_kick_requested(void);

/* ---- 踢除 (由網頁執行緒呼叫) ---- */
/*
 * 要求踢除目前的 UART session。
 * 有人登入：設定踢除旗標，把登入者帳號複製到 user_out，回傳 1。
 * 沒人登入：不做任何事，回傳 0。
 */
int state_request_kick(char *user_out, size_t n);

/* ---- 程式執行狀態 ---- */
/* 程式是否繼續執行 */
int app_running(void);
/* 要求程式結束。只寫一個 sig_atomic_t 旗標，可以安全地在 signal handler 中呼叫 */
void app_stop(void);

#endif /* STATE_H */
