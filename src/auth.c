#include <string.h>
#include <stdio.h>
#include "auth.h"

/*
 * Role-Based Authorization
 * 
 * Hard-coded user table
 *
 *  admin   / admin123  -> ROLE_ADMIN
 *  alice   / alice123  -> ROLE_EDITOR
 *  bob     / bob123    -> ROLE_EDITOR
 *  guest   / (any)     -> ROLE_GUEST
 */

typedef struct {
    const char *username;
    const char *password;
    Role role;
} UserEntry;

static const UserEntry USER_TABLE[] = {
    { "admin", "admin123", ROLE_ADMIN  },
    { "alice", "alice123", ROLE_EDITOR },
    { "bob",   "bob123",   ROLE_EDITOR },
    { "guest", "",         ROLE_GUEST  },
};

static const int USER_TABLE_SIZE = (int)(sizeof(USER_TABLE) / sizeof(USER_TABLE[0]));

// Returns the role on success, -1 on failure
int auth_verify(const char *username, const char *password, Role *out_role) {
    for (int i = 0; i < USER_TABLE_SIZE; i++) {
        if (strcmp(USER_TABLE[i].username, username) == 0) {
            // Guest can log in with any password
            if (USER_TABLE[i].role == ROLE_GUEST ||
                strcmp(USER_TABLE[i].password, password) == 0) {
                *out_role = USER_TABLE[i].role;
                return (int)USER_TABLE[i].role;
            }
            return -1; // wrong password
        }
    }
    return -1; // user not found
}

int auth_can_write(Role r) {
    return r == ROLE_EDITOR || r == ROLE_ADMIN;
}

int auth_can_kick(Role r) {
    return r == ROLE_ADMIN;
}

int auth_can_save(Role r) {
    return r == ROLE_ADMIN;
}

const char *auth_role_name(Role r) {
    switch (r) {
        case ROLE_GUEST:  return "guest";
        case ROLE_EDITOR: return "editor";
        case ROLE_ADMIN:  return "admin";
        default:          return "unknown";
    }
}