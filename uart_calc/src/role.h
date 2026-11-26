/*
 * role.h - 角色定義
 *
 * 系統固定三種角色：
 *   guest    : 可登入，只能用 help / status，永遠不能用計算機
 *   operator : 可登入並使用計算機
 *   admin    : 可登入並使用計算機
 * 角色在程式內以整數 (列舉) 表示，ROLE_NAME 用來轉成文字顯示。
 *
 * 這是最底層的模組，不依賴任何其他模組。
 */
#ifndef ROLE_H
#define ROLE_H

enum { ROLE_GUEST = 0, ROLE_OPERATOR, ROLE_ADMIN, ROLE_COUNT };

/* 角色編號 → 名稱，例如 ROLE_NAME[ROLE_OPERATOR] 是 "operator" */
extern const char *const ROLE_NAME[ROLE_COUNT];

/* 角色名稱字串 → 角色編號；找不到回傳 -1 */
int role_from_name(const char *s);

#endif /* ROLE_H */
