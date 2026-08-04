#ifndef DIRS_H
#define DIRS_H

#include <stddef.h>

/* XDG Base Directory Specification support.
 *
 * Resolves per-user directories following the XDG spec when applicable,
 * falling back to the legacy ~/.nash/ layout for existing installations.
 *
 * The nash_dirs_t struct is resolved once at startup and read-only afterward.
 * Thread-safe by construction: no mutation after nash_dirs_resolve() returns. */

typedef struct {
  char *config_dir; /* XDG_CONFIG_HOME/nash or ~/.nash */
  char *data_dir;   /* XDG_DATA_HOME/nash   or ~/.nash */
  char *state_dir;  /* XDG_STATE_HOME/nash   or ~/.nash */
  char *cache_dir;  /* XDG_CACHE_HOME/nash   or ~/.nash */
  int xdg_mode;     /* 1 = split XDG layout, 0 = legacy single-dir */
} nash_dirs_t;

/* Find config.toml before full directory resolution.
 *
 * Checks (in order):
 *   1. $XDG_CONFIG_HOME/nash/config.toml (or ~/.config/nash/config.toml)
 *   2. ~/.nash/config.toml
 *
 * Writes the found (or default) path into config_path_out (up to out_sz bytes).
 * Returns 0 on success, -1 on failure (HOME not set). */
int nash_dirs_find_config(char *config_path_out, size_t out_sz);

/* Resolve all directories.
 *
 * If data_dir_override is non-NULL, all four fields are set to that path
 * and xdg_mode is 0 (--data-dir is the rule).
 *
 * Otherwise, detects mode from where config.toml was found:
 *   - XDG config path exists  → xdg_mode = 1
 *   - ~/.nash/config.toml exists → xdg_mode = 0
 *   - Neither (fresh install) → xdg_mode = 1
 *
 * Creates all directories (mkdir -p).
 * Returns heap-allocated struct. Caller must call nash_dirs_free(). */
nash_dirs_t *nash_dirs_resolve(const char *data_dir_override);

/* Free all allocated paths and the struct itself. */
void nash_dirs_free(nash_dirs_t *dirs);

#endif /* DIRS_H */
