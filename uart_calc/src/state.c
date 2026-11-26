/*
 * state.c - 共享狀態的實作
 *
 * 除了 s_running 之外，所有變數讀寫都必須持有 s_mtx 鎖。
 * 因為這些變數都是 static，只有本檔案能直接存取，鎖的規則只要在這裡遵守即可。
 */
#include "state.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static state_snapshot_t s_st;   /* 目前的共享狀態 */
static int s_kick_request;      /* 網頁端要求踢除 UART */

/*
 * 程式是否繼續執行。收到 SIGINT/SIGTERM 時設為 0，各執行緒看到後會自行結束。
 * volatile sig_atomic_t：保證在 signal handler 中寫入是安全的。
 */
static volatile sig_atomic_t s_running = 1;

void state_init(const int allow[ROLE_COUNT]) {
    pthread_mutex_lock(&s_mtx);
    memset(&s_st, 0, sizeof s_st);
    for (int i = 0; i < ROLE_COUNT; i++) s_st.role_enabled[i] = allow[i];
    s_kick_request = 0;
    pthread_mutex_unlock(&s_mtx);
}

void state_snapshot(state_snapshot_t *out) {
    pthread_mutex_lock(&s_mtx);
    *out = s_st;   /* 在鎖內整個結構複製一份，之後在鎖外使用 */
    pthread_mutex_unlock(&s_mtx);
}

int state_role_enabled(int role) {
    pthread_mutex_lock(&s_mtx);
    int en = s_st.role_enabled[role];
    pthread_mutex_unlock(&s_mtx);
    return en;
}

void state_set_role_enabled(int role, int enabled) {
    pthread_mutex_lock(&s_mtx);
    s_st.role_enabled[role] = enabled;
    pthread_mutex_unlock(&s_mtx);
}

void state_session_begin(const char *user, int role) {
    pthread_mutex_lock(&s_mtx);
    s_st.uart_active = 1;
    snprintf(s_st.uart_user, sizeof s_st.uart_user, "%s", user);
    s_st.uart_role = role;
    s_st.uart_login_time = time(NULL);
    s_kick_request = 0;   /* 清除先前可能殘留的踢除請求 */
    pthread_mutex_unlock(&s_mtx);
}

void state_session_end(void) {
    pthread_mutex_lock(&s_mtx);
    s_st.uart_active = 0;
    s_st.uart_user[0] = '\0';
    s_kick_request = 0;
    pthread_mutex_unlock(&s_mtx);
}

int state_kick_requested(void) {
    pthread_mutex_lock(&s_mtx);
    int k = s_kick_request;
    pthread_mutex_unlock(&s_mtx);
    return k;
}

int state_request_kick(char *user_out, size_t n) {
    pthread_mutex_lock(&s_mtx);
    int active = s_st.uart_active;
    snprintf(user_out, n, "%s", s_st.uart_user);
    if (active) s_kick_request = 1;
    pthread_mutex_unlock(&s_mtx);
    return active;
}

int app_running(void) { return s_running; }

void app_stop(void) { s_running = 0; }
