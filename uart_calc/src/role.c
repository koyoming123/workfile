/*
 * role.c - 角色名稱與編號的轉換
 */
#include "role.h"

#include <string.h>

const char *const ROLE_NAME[ROLE_COUNT] = {"guest", "operator", "admin"};

int role_from_name(const char *s) {
    for (int i = 0; i < ROLE_COUNT; i++)
        if (strcmp(s, ROLE_NAME[i]) == 0) return i;
    return -1;
}
