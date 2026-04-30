/*
 * - I/O Multiplexing (poll): Watching STDIN and the socket simultaneously
 * without blocking on either.
 * - Non-Canonical Terminal I/O: Using ncurses to intercept keystrokes 
 * instantly without waiting for 'Enter'.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <ncurses.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"
#include "ot_engine.h"

// Local Document State
static char local_doc[MAX_DOC_SIZE];
static int local_len  = 0;
static int my_id      = -1;
static Role my_role    = ROLE_GUEST;
static VectorClock my_vc;

static int sock_fd = -1;
static volatile int client_running = 1;

// Editor UI State
static int cursor_pos = 0; // Logical index in local_doc array
static char status_msg[1024] = "Connected. Press F1 to Quit, F2 to Save.";

// Operation history for local OT
#define LOCAL_HIST_SIZE 64
static Operation local_hist[LOCAL_HIST_SIZE];
static int local_hist_head  = 0;
static int local_hist_count = 0;

static void push_local_hist(const Operation *op) {
    local_hist[local_hist_head] = *op;
    local_hist_head = (local_hist_head + 1) % LOCAL_HIST_SIZE;
    if (local_hist_count < LOCAL_HIST_SIZE) local_hist_count++;
}

// Screen Rendering (ncurses)
static void render_screen() {
    clear();
    // Draw Header
    attron(A_REVERSE);
    mvprintw(0, 0, " Tekstredaktilo | Role: %-7s | ID: %d ", 
             my_role == ROLE_ADMIN ? "ADMIN" : (my_role == ROLE_EDITOR ? "EDITOR" : "GUEST"), 
             my_id);
    attroff(A_REVERSE);
    // Draw Document Text
    mvprintw(2, 0, "%s", local_doc);
    // Draw Footer / Status Bar
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    mvprintw(max_y - 1, 0, ">> %s", status_msg);
    // Calculate physical (y, x) for logical cursor_pos
    int cur_y = 2, cur_x = 0;
    for (int i = 0; i < cursor_pos && i < local_len; i++) {
        if (local_doc[i] == '\n') {
            cur_y++;
            cur_x = 0;
        } else {
            cur_x++;
            if (cur_x >= max_x) { cur_x = 0; cur_y++; } // Handle wrap
        }
    }
    move(cur_y, cur_x); // Place physical terminal cursor
    refresh();
}

// Send & Apply Local Operation
static void send_local_op(Operation *op) {
    op->client_id = my_id;
    vc_increment(&my_vc, my_id);
    op->vc = my_vc;
    apply_operation(local_doc, MAX_DOC_SIZE, op);
    local_len = (int)strlen(local_doc);
    push_local_hist(op);
    send(sock_fd, op, sizeof(Operation), MSG_NOSIGNAL);
}

// Process Remote Operation (with OT)
static void apply_remote(Operation *op) {
    // Transform against unacknowledged local ops
    for (int i = 0; i < local_hist_count; i++) {
        int idx = (local_hist_head - local_hist_count + i + LOCAL_HIST_SIZE) % LOCAL_HIST_SIZE;
        int cmp = vc_compare(&op->vc, &local_hist[idx].vc);
        if (cmp == -1 || cmp == 2) ot_transform(op, &local_hist[idx]);
    }

    apply_operation(local_doc, MAX_DOC_SIZE, op);
    local_len = (int)strlen(local_doc);
    vc_merge(&my_vc, &op->vc);

    // Shift local cursor if someone typed before or at our cursor
    if (op->type == OP_INSERT && op->position <= cursor_pos) {
        cursor_pos += strlen(op->text);
    } else if (op->type == OP_DELETE && op->position < cursor_pos) {
        if (op->position + op->length <= cursor_pos) cursor_pos -= op->length;
        else cursor_pos = op->position; 
    }
}

int main(int argc, char *argv[]) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";

    // 1. Standard Socket Setup
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = htons(SERVER_PORT);
    inet_pton(AF_INET, host, &saddr.sin_addr);
    if (connect(sock_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("connect"); return 1;
    }
    // 2. Authentication (Standard CLI before entering UI mode)
    AuthRequest areq;
    memset(&areq, 0, sizeof(areq));
    printf("Username: "); fflush(stdout);
    fgets(areq.username, MAX_USERNAME, stdin);
    areq.username[strcspn(areq.username, "\n")] = '\0';
    printf("Password: "); fflush(stdout);
    fgets(areq.password, MAX_PASSWORD, stdin);
    areq.password[strcspn(areq.password, "\n")] = '\0';
    send(sock_fd, &areq, sizeof(areq), 0);
    Operation resp;
    recv(sock_fd, &resp, sizeof(resp), MSG_WAITALL);
    if (resp.type != OP_AUTH_OK) {
        printf("Authentication failed or denied.\n");
        close(sock_fd); return 1;
    }
    my_id   = resp.client_id;
    my_role = (Role)resp.position;
    memset(&my_vc, 0, sizeof(my_vc));
    memset(local_doc, 0, sizeof(local_doc));
    // 3. Initialize ncurses (Switch to Non-Canonical Mode)
    initscr();             // Start ncurses mode
    cbreak();              // Disable line buffering (instant keystrokes)
    noecho();              // Don't print typed characters automatically
    keypad(stdscr, TRUE);  // Enable F-keys and Arrows
    nodelay(stdscr, TRUE); // Make getch() non-blocking

    // 4. I/O Multiplexing Setup
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO; // Watch Keyboard
    fds[0].events = POLLIN;
    fds[1].fd = sock_fd;      // Watch Server Socket
    fds[1].events = POLLIN;

    render_screen();

    // 5. The Live Event Loop
    while (client_running) {
        int poll_count = poll(fds, 2, 50); // Wait up to 50ms for events
        if (poll_count < 0) break;

        int needs_redraw = 0;

        // --- EVENT: Keyboard Input Detected ---
        if (fds[0].revents & POLLIN) {
            int ch = getch();
            if (ch != ERR) {
                if (ch == KEY_F(1)) { // F1 to Quit
                    client_running = 0;
                } 
                else if (ch == KEY_F(2)) { // F2 to Save
                    Operation op; memset(&op, 0, sizeof(op));
                    op.type = OP_MSG; op.position = 0xCAFE; op.client_id = my_id;
                    send(sock_fd, &op, sizeof(op), 0);
                    snprintf(status_msg, sizeof(status_msg), "Save request sent.");
                }
                else if (ch == KEY_LEFT && cursor_pos > 0) cursor_pos--;
                else if (ch == KEY_RIGHT && cursor_pos < local_len) cursor_pos++;
                else if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                    if (cursor_pos > 0 && my_role != ROLE_GUEST) {
                        Operation op; memset(&op, 0, sizeof(op));
                        op.type = OP_DELETE; op.position = cursor_pos - 1; op.length = 1;
                        send_local_op(&op);
                        cursor_pos--;
                    }
                }
                else if (ch >= 32 && ch <= 126 && my_role != ROLE_GUEST) { // Printable chars
                    if (local_len < MAX_DOC_SIZE - 1) {
                        Operation op; memset(&op, 0, sizeof(op));
                        op.type = OP_INSERT; op.position = cursor_pos;
                        op.text[0] = ch; op.text[1] = '\0';
                        send_local_op(&op);
                        cursor_pos++;
                    }
                }
                needs_redraw = 1;
            }
        }

        // --- EVENT: Server Network Packet Detected ---
        if (fds[1].revents & POLLIN) {
            Operation op;
            int n = recv(sock_fd, &op, sizeof(op), MSG_WAITALL);
            if (n <= 0) {
                client_running = 0;
                break;
            }

            if (op.type == OP_SYNC) {
                SyncPacket pkt; recv(sock_fd, &pkt, sizeof(pkt), MSG_WAITALL);
                memcpy(local_doc, pkt.doc, pkt.doc_len + 1);
                local_len = pkt.doc_len;
                if (cursor_pos > local_len) cursor_pos = local_len;
                local_hist_count = 0; local_hist_head = 0;
            } 
            else if (op.type == OP_ACK) {
                vc_merge(&my_vc, &op.vc);
            } 
            else if (op.type == OP_INSERT || op.type == OP_DELETE) {
                apply_remote(&op);
            }
            else if (op.type == OP_AUTH_FAIL) {
                snprintf(status_msg, sizeof(status_msg), "ERROR: Permission Denied.");
            }
            else if (op.type == OP_MSG) {
                snprintf(status_msg, sizeof(status_msg), "Server: %s", op.text);
            }
            needs_redraw = 1;
        }

        if (needs_redraw) {
            render_screen();
        }
    }

    // Cleanup UI
    endwin(); 
    close(sock_fd);
    printf("Disconnected from Tekstredaktilo server.\n");
    return 0;
}