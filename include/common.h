#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

// System limits and constants
#define SERVER_PORT      9090
#define MAX_CLIENTS      16
#define MAX_DOC_SIZE     65536   // 64 KB
#define MAX_USERNAME     32
#define MAX_PASSWORD     64
#define MAX_OP_TEXT      512
#define LOG_PIPE         "/tmp/collab_editor_log"  // named FIFO for IPC logger

// Roles
typedef enum {
    ROLE_GUEST  = 0,   // read-only
    ROLE_EDITOR = 1,   // insert, delete
    ROLE_ADMIN  = 2    // insert, delete, kick, save
} Role;

// Operation types
typedef enum {
    OP_INSERT  = 1,
    OP_DELETE  = 2,
    OP_ACK     = 3,   // server -> client: operation accepted
    OP_SYNC    = 4,   // server -> client: full doc sync
    OP_AUTH    = 5,   // client -> server: login request
    OP_AUTH_OK = 6,
    OP_AUTH_FAIL = 7,
    OP_KICK    = 8,   // admin -> server: kick a user
    OP_MSG     = 9    // informational message
} OpType;

// Vector clock
#define MAX_VC_SIZE MAX_CLIENTS
typedef struct {
    uint32_t clock[MAX_VC_SIZE];  // one slot per client id
    int      size;
} VectorClock;

// Network packet
typedef struct {
    OpType      type;
    int         client_id;       // originating client
    int         position;        // character index in document
    char        text[MAX_OP_TEXT]; // inserted text  (OP_INSERT)
    int         length;          // chars to delete (OP_DELETE)
    VectorClock vc;              // CRDT: causality vector
    uint64_t    timestamp;       // wall-clock tie-breaker
} Operation;

// Auth request (sent at login)
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
} AuthRequest;

// Server -> Client sync packet (sent when the server approves the login)
typedef struct {
    int  doc_len;
    char doc[MAX_DOC_SIZE];
} SyncPacket;

#endif