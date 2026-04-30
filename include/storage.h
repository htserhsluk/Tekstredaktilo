#ifndef STORAGE_H
#define STORAGE_H

/* Load the document from disk into buf (up to buf_size bytes).
 * Uses fcntl read-lock while loading.
 * Returns number of bytes read, or -1 on error. */
int storage_load(const char *path, char *buf, int buf_size);

/* Persist buf to disk atomically (write to tmp, rename).
 * Uses fcntl write-lock while writing.
 * Returns 0 on success, -1 on error. */
int storage_save(const char *path, const char *buf, int len);

#endif