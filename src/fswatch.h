/* fswatch.h — Platform-abstracted filesystem directory watcher.
 *
 * Uses inotify on Linux, kqueue on macOS.  Falls back to polling
 * if the platform-specific init fails at runtime.
 *
 * Callers receive "directory changed" notifications — not filenames.
 * After fswatch_wait() returns 1, the caller scans the directory. */

#ifndef FSWATCH_H
#define FSWATCH_H

typedef struct fswatch fswatch_t;

/* Create a new watcher.  Returns NULL only on malloc failure.
 * If the platform backend (inotify/kqueue) fails to init, the
 * watcher falls back to polling mode transparently. */
fswatch_t *fswatch_init(void);

/* Watch a directory for file creation / move-in events.
 * Returns 0 on success (including when the backend fails and the watcher
 * transparently degrades to polling mode), -1 on invalid arguments. */
int fswatch_add(fswatch_t *w, const char *dir_path);

/* Block until the watched directory changes or timeout expires.
 * timeout_ms: >0 = milliseconds, 0 = non-blocking check, -1 = wait forever.
 * Returns: 1 = directory changed (caller should scan),
 *          0 = timeout with no change,
 *         -1 = error or interrupted by signal. */
int fswatch_wait(fswatch_t *w, int timeout_ms);

/* Release all resources.  Safe to call with NULL. */
void fswatch_close(fswatch_t *w);

#endif /* FSWATCH_H */
