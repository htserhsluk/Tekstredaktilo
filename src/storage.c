#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "storage.h"

/* File Locking
 * 
 * load → acquires F_RDLCK, releases after read
 * save → acquires F_WRLCK, writes atomically (tmp + rename), releases
 */

static int set_lock(int fd, short type) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0; // lock entire file
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("[storage] fcntl lock");
        return -1;
    }
    return 0;
}

static int release_lock(int fd) {
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    fcntl(fd, F_SETLK, &fl);
    return 0;
}

int storage_load(const char *path, char *buf, int buf_size) {
    int fd = open(path, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) {
        if (errno == ENOENT) { buf[0] = '\0'; return 0; }
        perror("[storage] open for read");
        return -1;
    }
    if (set_lock(fd, F_RDLCK) < 0) { close(fd); return -1; }
    int total = 0;
    int n;
    while (total < buf_size - 1 &&
           (n = (int)read(fd, buf + total, buf_size - 1 - total)) > 0)
        total += n;

    buf[total] = '\0';
    release_lock(fd);
    close(fd);
    return total;
}

int storage_save(const char *path, const char *buf, int len) {
    /* Write to a temporary file first, then rename for atomicity */
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("[storage] open tmp"); return -1; }
    if (set_lock(fd, F_WRLCK) < 0) { close(fd); return -1; }
    int written = 0;
    while (written < len) {
        int n = (int)write(fd, buf + written, len - written);
        if (n <= 0) { perror("[storage] write"); release_lock(fd); close(fd); return -1; }
        written += n;
    }
    release_lock(fd);
    close(fd);
    /* Atomic rename */
    if (rename(tmp_path, path) < 0) {
        perror("[storage] rename");
        return -1;
    }
    return 0;
}