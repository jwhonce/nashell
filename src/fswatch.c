/* fswatch.c — Platform-abstracted filesystem directory watcher.
 *
 * Backend selection:
 *   Linux:  inotify (IN_CREATE | IN_MOVED_TO)
 *   macOS:  kqueue  (EVFILT_VNODE, NOTE_WRITE)
 *   Other:  compile-time error
 *
 * If the platform backend fails to initialise at runtime the watcher
 * enters polling mode: fswatch_wait() sleeps for the requested timeout
 * and returns 1 so the caller scans the directory unconditionally. */

#include "fswatch.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <poll.h>
#include <sys/inotify.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "Unsupported platform — requires Linux (inotify) or macOS (kqueue)"
#endif

struct fswatch {
  int fallback; /* 1 = platform backend failed, use polling */
#if defined(__linux__)
  int ifd;      /* inotify fd */
  int wd;       /* watch descriptor */
#elif defined(__APPLE__)
  int kqfd;     /* kqueue fd */
  int dirfd;    /* open directory fd for EVFILT_VNODE */
#endif
};

/* ── Init ──────────────────────────────────────────────── */

fswatch_t *fswatch_init(void) {
  fswatch_t *w = calloc(1, sizeof(*w));
  if (!w) return NULL;

#if defined(__linux__)
  w->ifd = inotify_init1(IN_NONBLOCK);
  w->wd = -1;
  if (w->ifd < 0) {
    w->fallback = 1;
  }
#elif defined(__APPLE__)
  w->kqfd = kqueue();
  w->dirfd = -1;
  if (w->kqfd < 0) {
    w->fallback = 1;
  }
#endif

  return w;
}

/* ── Add watch ─────────────────────────────────────────── */

int fswatch_add(fswatch_t *w, const char *dir_path) {
  if (!w || !dir_path) return -1;
  if (w->fallback) return 0; /* polling mode — nothing to register */

#if defined(__linux__)
  w->wd = inotify_add_watch(w->ifd, dir_path, IN_CREATE | IN_MOVED_TO);
  if (w->wd < 0) {
    w->fallback = 1;
    return 0; /* degrade to polling */
  }
#elif defined(__APPLE__)
  w->dirfd = open(dir_path, O_RDONLY | O_EVTONLY);
  if (w->dirfd < 0) {
    w->fallback = 1;
    return 0;
  }
  struct kevent ev;
  EV_SET(&ev, (uintptr_t)w->dirfd, EVFILT_VNODE,
         EV_ADD | EV_CLEAR, NOTE_WRITE, 0, NULL);
  if (kevent(w->kqfd, &ev, 1, NULL, 0, NULL) < 0) {
    close(w->dirfd);
    w->dirfd = -1;
    w->fallback = 1;
    return 0;
  }
#endif

  return 0;
}

/* ── Wait ──────────────────────────────────────────────── */

int fswatch_wait(fswatch_t *w, int timeout_ms) {
  if (!w) return -1;

  /* Polling fallback: sleep in 1-second chunks then tell caller to scan.
   * Caps each usleep to avoid useconds_t overflow on large timeouts. */
  if (w->fallback) {
    int remaining = timeout_ms;
    while (remaining > 0) {
      int chunk = remaining > 1000 ? 1000 : remaining;
      usleep((useconds_t)chunk * 1000);
      remaining -= chunk;
    }
    return 1;
  }

#if defined(__linux__)
  {
    struct pollfd pfd = {.fd = w->ifd, .events = POLLIN};
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0)
      return (errno == EINTR) ? -1 : -1;
    if (ret == 0)
      return 0; /* timeout */

    /* Drain the inotify event buffer so it doesn't accumulate */
    char buf[4096]
      __attribute__((aligned(__alignof__(struct inotify_event))));
    while (read(w->ifd, buf, sizeof(buf)) > 0)
      ; /* discard — callers scan the directory themselves */
    return 1;
  }
#elif defined(__APPLE__)
  {
    struct kevent ev;
    struct timespec ts;
    struct timespec *tsp = NULL;
    if (timeout_ms >= 0) {
      ts.tv_sec = timeout_ms / 1000;
      ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
      tsp = &ts;
    }
    int ret = kevent(w->kqfd, NULL, 0, &ev, 1, tsp);
    if (ret < 0)
      return -1;
    if (ret == 0)
      return 0; /* timeout */
    return 1;
  }
#endif
}

/* ── Close ─────────────────────────────────────────────── */

void fswatch_close(fswatch_t *w) {
  if (!w) return;

#if defined(__linux__)
  if (w->wd >= 0 && w->ifd >= 0)
    inotify_rm_watch(w->ifd, w->wd);
  if (w->ifd >= 0)
    close(w->ifd);
#elif defined(__APPLE__)
  if (w->dirfd >= 0)
    close(w->dirfd);
  if (w->kqfd >= 0)
    close(w->kqfd);
#endif

  free(w);
}
