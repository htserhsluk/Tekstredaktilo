#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/time.h>
#include "common.h"
#include "ot_engine.h"
#include "auth.h"
#include "storage.h"
#include "logger.h"

#define DOC_FILE  "data/document.txt"
#define OP_HISTORY_SIZE 256

// Global shared state
typedef struct {
    int fd;
    int id;
    char username[MAX_USERNAME];
    Role role;
    int active;
    VectorClock vc;       // client's last known vector clock
    pthread_t thread;
} ClientState;

static ClientState clients[MAX_CLIENTS];
static int client_count = 0;

// Document buffer – protected by doc_mutex
static char document[MAX_DOC_SIZE];
static int doc_len = 0;

// Operation history for OT (ring buffer)
static Operation op_history[OP_HISTORY_SIZE];
static int op_history_head = 0;
static int op_history_count = 0;

// Server vector clock (one slot per client)
static VectorClock  server_vc;

static pthread_mutex_t doc_mutex   = PTHREAD_MUTEX_INITIALIZER;
static sem_t pending_sem;

static int server_fd = -1;
static volatile int running = 1;

// Helpers
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}

static void send_op(int fd, const Operation *op) {
    send(fd, op, sizeof(Operation), MSG_NOSIGNAL);
}

static void send_sync(int fd) {
    // Send OP_SYNC so client can rebuild its view
    Operation hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = OP_SYNC;
    hdr.length = doc_len;
    send(fd, &hdr, sizeof(hdr), MSG_NOSIGNAL);
    SyncPacket pkt;
    pkt.doc_len = doc_len;
    memcpy(pkt.doc, document, doc_len + 1);
    send(fd, &pkt, sizeof(pkt), MSG_NOSIGNAL);
}

static void broadcast_op(const Operation *op, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].fd != exclude_fd)
            send_op(clients[i].fd, op);
    }
}

// OT: find ops the client hasn't seen and transform against them
static int server_transform_and_apply(Operation *op) {
    Operation transformed = *op;
    for (int i = 0; i < op_history_count; i++) {
        int idx = (op_history_head - op_history_count + i + OP_HISTORY_SIZE)
                  % OP_HISTORY_SIZE;
        Operation *hist = &op_history[idx];
        // Only transform against ops the client hasn't seen
        int cmp = vc_compare(&op->vc, &hist->vc);
        if (cmp == -1 || cmp == 2) {
            // hist happened after or concurrently with op -> transform
            if (ot_transform(&transformed, hist) < 0)
                return -1;
        }
    }

    // Apply to document
    if (apply_operation(document, MAX_DOC_SIZE, &transformed) < 0)
        return -1;
    doc_len = (int)strlen(document);

    // Record in history
    vc_increment(&server_vc, transformed.client_id);
    transformed.vc = server_vc;
    op_history[op_history_head] = transformed;
    op_history_head = (op_history_head + 1) % OP_HISTORY_SIZE;
    if (op_history_count < OP_HISTORY_SIZE) op_history_count++;
    *op = transformed; // caller gets updated op with server VC
    return 0;
}

// Per-client thread
static void remove_client(int id) {
    if (id < 0 || id >= MAX_CLIENTS) return;
    if (clients[id].fd >= 0) close(clients[id].fd);
    clients[id].active = 0;
    clients[id].fd     = -1;
    client_count--;
}

static void *client_thread(void *arg) {
    ClientState *cs = (ClientState *)arg;
    logger_write("Client %d (%s / %s) connected", cs->id, cs->username, auth_role_name(cs->role));
    // Send initial document state
    pthread_mutex_lock(&doc_mutex);
    send_sync(cs->fd);
    pthread_mutex_unlock(&doc_mutex);
    Operation op;
    while (running) {
        int n = (int)recv(cs->fd, &op, sizeof(op), MSG_WAITALL);
        if (n <= 0) break;
        // KICK command (admin only)
        if (op.type == OP_KICK) {
            if (!auth_can_kick(cs->role)) {
                logger_write("DENIED: %s tried to kick (not admin)", cs->username);
                continue;
            }
            int target = op.client_id;
            if (target >= 0 && target < MAX_CLIENTS && clients[target].active) {
                logger_write("ADMIN %s kicked client %d (%s)", cs->username, target, clients[target].username);
                pthread_mutex_lock(&doc_mutex);
                remove_client(target);
                pthread_mutex_unlock(&doc_mutex);
            }
            continue;
        }
        // Save command (admin only)
        if (op.type == OP_MSG && op.position == 0xCAFE) {
            if (!auth_can_save(cs->role)) {
                logger_write("DENIED: %s tried to save (not admin)", cs->username);
                continue;
            }
            pthread_mutex_lock(&doc_mutex);
            storage_save(DOC_FILE, document, doc_len);
            pthread_mutex_unlock(&doc_mutex);
            logger_write("Document saved by admin %s", cs->username);
            continue;
        }

        // Write operations
        if (op.type == OP_INSERT || op.type == OP_DELETE) {
            if (!auth_can_write(cs->role)) {
                logger_write("DENIED: %s (guest) tried to write", cs->username);
                /* send nack */
                Operation nack;
                memset(&nack, 0, sizeof(nack));
                nack.type = OP_AUTH_FAIL;
                send_op(cs->fd, &nack);
                continue;
            }
            op.client_id = cs->id;
            op.timestamp = now_us();
            vc_merge(&cs->vc, &op.vc);

            // Critical section: transform + apply
            pthread_mutex_lock(&doc_mutex);
            int ok = server_transform_and_apply(&op);
            pthread_mutex_unlock(&doc_mutex);
            if (ok < 0) {
                logger_write("OT failed for client %d, sending sync", cs->id);
                pthread_mutex_lock(&doc_mutex);
                send_sync(cs->fd);
                pthread_mutex_unlock(&doc_mutex);
                continue;
            }
            logger_write("OP type=%d pos=%d len=%d by %s", op.type, op.position, op.length, cs->username);
            // ACK to originator
            Operation ack = op;
            ack.type = OP_ACK;
            send_op(cs->fd, &ack);
            // Broadcast transformed op to everyone else
            pthread_mutex_lock(&doc_mutex);
            broadcast_op(&op, cs->fd);
            pthread_mutex_unlock(&doc_mutex);
            // Signal semaphore
            sem_post(&pending_sem);
        }
    }

    pthread_mutex_lock(&doc_mutex);
    logger_write("Client %d (%s) disconnected", cs->id, cs->username);
    remove_client(cs->id);
    pthread_mutex_unlock(&doc_mutex);
    return NULL;
}

// Auto-save thread – wakes on semaphore, saves every N ops
#define AUTOSAVE_EVERY 10

static void *autosave_thread(void *arg) {
    (void)arg;
    int op_since_save = 0;
    while (running) {
        sem_wait(&pending_sem);
        if (!running) break;
        op_since_save++;
        if (op_since_save >= AUTOSAVE_EVERY) {
            pthread_mutex_lock(&doc_mutex);
            storage_save(DOC_FILE, document, doc_len);
            pthread_mutex_unlock(&doc_mutex);
            logger_write("[autosave] Document saved after %d ops", op_since_save);
            op_since_save = 0;
        }
    }
    return NULL;
}

// Signal handler for graceful shutdown
static void handle_signal(int sig) {
    (void)sig;
    running = 0;
    if (server_fd >= 0) close(server_fd);
    sem_post(&pending_sem); // wake autosave thread
}

int main(void) {
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    // IPC: named pipe logger
    logger_init_writer();
    logger_write("=== Collaborative Text Editor Server starting ===");
    // Init semaphore (unnamed, process-local)
    sem_init(&pending_sem, 0, 0);

    // Init client table
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd     = -1;
        clients[i].active = 0;
    }
    memset(&server_vc, 0, sizeof(server_vc));

    // Load document from disk (fcntl read-lock inside)
    doc_len = storage_load(DOC_FILE, document, MAX_DOC_SIZE);
    if (doc_len < 0) doc_len = 0;
    logger_write("Loaded document (%d bytes)", doc_len);

    // Auto-save thread
    pthread_t save_tid;
    pthread_create(&save_tid, NULL, autosave_thread, NULL);

    // Socket Programming
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(SERVER_PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); return 1;
    }
    logger_write("Listening on port %d ...", SERVER_PORT);
    printf("Server ready. Connect clients on port %d\n", SERVER_PORT);

    // Accept loop
    while (running) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EINTR || !running) break;
            perror("accept");
            continue;
        }
        // Authenticate
        AuthRequest areq;
        memset(&areq, 0, sizeof(areq));
        int rn = (int)recv(cfd, &areq, sizeof(areq), MSG_WAITALL);
        if (rn <= 0) { close(cfd); continue; }
        Role role;
        int auth_result = auth_verify(areq.username, areq.password, &role);
        Operation resp;
        memset(&resp, 0, sizeof(resp));
        if (auth_result < 0) {
            resp.type = OP_AUTH_FAIL;
            send(cfd, &resp, sizeof(resp), 0);
            logger_write("AUTH FAIL: user='%s' from %s", areq.username, inet_ntoa(caddr.sin_addr));
            close(cfd);
            continue;
        }
        // Find a free slot
        pthread_mutex_lock(&doc_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) { slot = i; break; }
        }
        if (slot < 0) {
            pthread_mutex_unlock(&doc_mutex);
            resp.type = OP_MSG;
            strncpy(resp.text, "Server full", MAX_OP_TEXT - 1);
            send(cfd, &resp, sizeof(resp), 0);
            close(cfd);
            logger_write("Rejected %s: server full", areq.username);
            continue;
        }
        clients[slot].fd = cfd;
        clients[slot].id = slot;
        clients[slot].role = role;
        clients[slot].active = 1;
        strncpy(clients[slot].username, areq.username, MAX_USERNAME - 1);
        memset(&clients[slot].vc, 0, sizeof(VectorClock));
        client_count++;
        pthread_mutex_unlock(&doc_mutex);

        // Send AUTH_OK with assigned id and role
        resp.type      = OP_AUTH_OK;
        resp.client_id = slot;
        resp.position  = (int)role;
        send(cfd, &resp, sizeof(resp), 0);

        // Spawn client thread
        pthread_create(&clients[slot].thread, NULL, client_thread, &clients[slot]);
        pthread_detach(clients[slot].thread);
    }

    // Graceful shutdown
    logger_write("Server shutting down...");
    running = 0;
    sem_post(&pending_sem);
    pthread_join(save_tid, NULL);
    pthread_mutex_lock(&doc_mutex);
    storage_save(DOC_FILE, document, doc_len);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].active) close(clients[i].fd);
    pthread_mutex_unlock(&doc_mutex);
    sem_destroy(&pending_sem);
    logger_close();
    printf("Server stopped.\n");
    return 0;
}