#ifndef AUTH_H
#define AUTH_H
#include "common.h"

// Verify credentials; returns the user's Role or -1 on failure
int auth_verify(const char *username, const char *password, Role *out_role);

// Permission checks
int auth_can_write(Role r);   // editor or admin
int auth_can_kick(Role r);    // admin only
int auth_can_save(Role r);    // admin only

const char *auth_role_name(Role r);

#endif