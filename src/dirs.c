/* dirs.c — XDG Base Directory Specification support.
 *
 * See https://specifications.freedesktop.org/basedir/latest/
 *
 * Resolution priority:
 *   1. data_dir_override (--data-dir) → all dirs = override, xdg_mode = 0
 *   2. Detect from config file location:
 *      a. XDG config path exists → xdg_mode = 1
 *      b. ~/.nash/config.toml exists → xdg_mode = 0
 *      c. Fresh install → xdg_mode = 1
 *   3. Build paths from XDG env vars or defaults */

#include "dirs.h"
#include "str.h"
#include "nash_limits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build an XDG directory path: $env_var/nash or $HOME/default_suffix/nash.
 * Returns heap-allocated string. */
static char *xdg_dir(const char *env_var, const char *default_suffix,
                     const char *home) {
  const char *base = getenv(env_var);
  char path[NASH_PATH_MAX];
  if (base && base[0])
    snprintf(path, sizeof(path), "%s/nash", base);
  else
    snprintf(path, sizeof(path), "%s/%s/nash", home, default_suffix);
  return xstrdup(path);
}

int nash_dirs_find_config(char *config_path_out, size_t out_sz) {
  const char *home = getenv("HOME");
  if (!home || !home[0])
    home = "/tmp";

  /* 1. Check XDG config path */
  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  char xdg_dir_path[NASH_PATH_MAX];
  if (xdg_config && xdg_config[0])
    snprintf(xdg_dir_path, sizeof(xdg_dir_path), "%s/nash", xdg_config);
  else
    snprintf(xdg_dir_path, sizeof(xdg_dir_path), "%s/.config/nash", home);

  snprintf(config_path_out, out_sz, "%s/config.toml", xdg_dir_path);
  if (file_exists(config_path_out))
    return 0;

  /* 2. Check legacy path */
  char legacy[NASH_PATH_MAX];
  snprintf(legacy, sizeof(legacy), "%s/.nash/config.toml", home);
  if (file_exists(legacy)) {
    snprintf(config_path_out, out_sz, "%s", legacy);
    return 0;
  }

  /* 3. Fresh install — config_path_out already has XDG path from step 1 */
  return 0;
}

nash_dirs_t *nash_dirs_resolve(const char *data_dir_override) {
  nash_dirs_t *d = xcalloc(1, sizeof(*d));

  /* Rule: --data-dir overrides everything */
  if (data_dir_override && data_dir_override[0]) {
    d->config_dir = xstrdup(data_dir_override);
    d->data_dir = d->config_dir;
    d->xdg_mode = 0;
    mkdir_p(data_dir_override, 0755);
    return d;
  }

  const char *home = getenv("HOME");
  if (!home || !home[0])
    home = "/tmp";

  /* Detect mode from config file location */
  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  char xdg_config_dir[NASH_PATH_MAX];
  if (xdg_config && xdg_config[0])
    snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/nash", xdg_config);
  else
    snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/.config/nash", home);

  char legacy_dir[NASH_PATH_MAX];
  snprintf(legacy_dir, sizeof(legacy_dir), "%s/.nash", home);

  char probe[NASH_PATH_MAX];
  snprintf(probe, sizeof(probe), "%s/config.toml", xdg_config_dir);

  if (file_exists(probe)) {
    d->xdg_mode = 1;
  } else {
    snprintf(probe, sizeof(probe), "%s/config.toml", legacy_dir);
    if (file_exists(probe))
      d->xdg_mode = 0;
    else
      d->xdg_mode = 1; /* fresh install → XDG */
  }

  if (d->xdg_mode == 0) {
    /* Legacy: all dirs point to ~/.nash */
    d->config_dir = xstrdup(legacy_dir);
    d->data_dir = d->config_dir;
  } else {
    d->config_dir = xdg_dir("XDG_CONFIG_HOME", ".config", home);
    d->data_dir = xdg_dir("XDG_DATA_HOME", ".local/share", home);
  }

  mkdir_p(d->config_dir, 0755);
  mkdir_p(d->data_dir, 0755);

  if (d->xdg_mode && dir_exists(legacy_dir))
    fprintf(stderr,
            "[warn] Legacy directory %s exists but nash is using XDG layout:\n"
            "  config: %s\n"
            "  data:   %s\n"
            "[warn] Data in %s will NOT be used. To migrate:\n"
            "  mv %s/* %s/\n"
            "  rmdir %s\n",
            legacy_dir, d->config_dir, d->data_dir,
            legacy_dir, legacy_dir, d->data_dir, legacy_dir);

  return d;
}

void nash_dirs_free(nash_dirs_t *dirs) {
  if (!dirs) return;
  char *base = dirs->config_dir;
  if (dirs->data_dir != base) free(dirs->data_dir);
  free(base);
  free(dirs);
}
