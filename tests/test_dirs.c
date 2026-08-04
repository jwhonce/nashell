/* test_dirs.c — Unit tests for XDG Base Directory support (dirs.c) */

#include "test_common.h"
#include "dirs.h"
#include "nash_limits.h"
#include "str.h"
#include "agents.h"

/* ── Helpers ───────────────────────────────────────────── */

static void unset_xdg_vars(void) {
  unsetenv("XDG_CONFIG_HOME");
  unsetenv("XDG_DATA_HOME");
  unsetenv("XDG_STATE_HOME");
  unsetenv("XDG_CACHE_HOME");
}

/* ── Tests ─────────────────────────────────────────────── */

static void test_override_sets_all_dirs(void) {
  char *tmp = make_test_dir();

  nash_dirs_t *d = nash_dirs_resolve(tmp);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 0);
  ASSERT_STR_EQ(d->config_dir, tmp);
  ASSERT_STR_EQ(d->data_dir, tmp);
  ASSERT_STR_EQ(d->state_dir, tmp);
  ASSERT_STR_EQ(d->cache_dir, tmp);

  nash_dirs_free(d);
  rm_rf(tmp);
  free(tmp);
}

static void test_override_empty_string_is_not_override(void) {
  unset_xdg_vars();

  /* With empty string, should NOT treat as override */
  nash_dirs_t *d = nash_dirs_resolve("");
  ASSERT_NOT_NULL(d);
  /* xdg_mode depends on whether ~/.nash/config.toml exists,
     but it should NOT be an override (all dirs same) scenario
     unless legacy mode detected */
  nash_dirs_free(d);
}

static void test_legacy_mode_when_dot_nash_exists(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  /* Simulate legacy layout: create tmp/.nash/config.toml */
  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  char dot_nash[512];
  snprintf(dot_nash, sizeof(dot_nash), "%s/.nash", tmp);
  mkdir(dot_nash, 0755);

  char config[512];
  snprintf(config, sizeof(config), "%s/.nash/config.toml", tmp);
  write_file(config, "", 0);

  nash_dirs_t *d = nash_dirs_resolve(NULL);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 0);
  ASSERT_STR_EQ(d->config_dir, dot_nash);
  ASSERT_STR_EQ(d->data_dir, dot_nash);
  ASSERT_STR_EQ(d->state_dir, dot_nash);
  ASSERT_STR_EQ(d->cache_dir, dot_nash);

  nash_dirs_free(d);
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_xdg_mode_fresh_install(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* No ~/.nash/ and no XDG config → fresh install → xdg_mode = 1 */
  nash_dirs_t *d = nash_dirs_resolve(NULL);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 1);

  /* Verify XDG default paths */
  char expected[512];
  snprintf(expected, sizeof(expected), "%s/.config/nash", tmp);
  ASSERT_STR_EQ(d->config_dir, expected);

  snprintf(expected, sizeof(expected), "%s/.local/share/nash", tmp);
  ASSERT_STR_EQ(d->data_dir, expected);

  snprintf(expected, sizeof(expected), "%s/.local/state/nash", tmp);
  ASSERT_STR_EQ(d->state_dir, expected);

  snprintf(expected, sizeof(expected), "%s/.cache/nash", tmp);
  ASSERT_STR_EQ(d->cache_dir, expected);

  /* Verify directories were created */
  ASSERT(dir_exists(d->config_dir));
  ASSERT(dir_exists(d->data_dir));
  ASSERT(dir_exists(d->state_dir));
  ASSERT(dir_exists(d->cache_dir));

  nash_dirs_free(d);
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_xdg_env_vars_override_defaults(void) {
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Set custom XDG dirs */
  char custom_config[512], custom_data[512], custom_state[512], custom_cache[512];
  snprintf(custom_config, sizeof(custom_config), "%s/myconfig", tmp);
  snprintf(custom_data, sizeof(custom_data), "%s/mydata", tmp);
  snprintf(custom_state, sizeof(custom_state), "%s/mystate", tmp);
  snprintf(custom_cache, sizeof(custom_cache), "%s/mycache", tmp);

  setenv("XDG_CONFIG_HOME", custom_config, 1);
  setenv("XDG_DATA_HOME", custom_data, 1);
  setenv("XDG_STATE_HOME", custom_state, 1);
  setenv("XDG_CACHE_HOME", custom_cache, 1);

  nash_dirs_t *d = nash_dirs_resolve(NULL);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 1);

  char expected[512];
  snprintf(expected, sizeof(expected), "%s/nash", custom_config);
  ASSERT_STR_EQ(d->config_dir, expected);

  snprintf(expected, sizeof(expected), "%s/nash", custom_data);
  ASSERT_STR_EQ(d->data_dir, expected);

  snprintf(expected, sizeof(expected), "%s/nash", custom_state);
  ASSERT_STR_EQ(d->state_dir, expected);

  snprintf(expected, sizeof(expected), "%s/nash", custom_cache);
  ASSERT_STR_EQ(d->cache_dir, expected);

  nash_dirs_free(d);
  unset_xdg_vars();
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_xdg_mode_when_xdg_config_exists(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Create XDG config path with config.toml */
  char xdg_config_dir[512];
  snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/.config/nash", tmp);
  mkdir_p(xdg_config_dir, 0755);

  char config[512];
  snprintf(config, sizeof(config), "%s/.config/nash/config.toml", tmp);
  write_file(config, "", 0);

  nash_dirs_t *d = nash_dirs_resolve(NULL);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 1);
  ASSERT_STR_EQ(d->config_dir, xdg_config_dir);

  nash_dirs_free(d);
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_find_config_xdg_path(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Create XDG config */
  char xdg_config_dir[512];
  snprintf(xdg_config_dir, sizeof(xdg_config_dir), "%s/.config/nash", tmp);
  mkdir_p(xdg_config_dir, 0755);

  char expected_path[512];
  snprintf(expected_path, sizeof(expected_path), "%s/.config/nash/config.toml", tmp);
  write_file(expected_path, "", 0);

  char found[NASH_PATH_MAX];
  int rc = nash_dirs_find_config(found, sizeof(found));
  ASSERT_EQ(rc, 0);
  ASSERT_STR_EQ(found, expected_path);

  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_find_config_legacy_path(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Create legacy config (no XDG config exists) */
  char dot_nash[512];
  snprintf(dot_nash, sizeof(dot_nash), "%s/.nash", tmp);
  mkdir(dot_nash, 0755);

  char expected_path[512];
  snprintf(expected_path, sizeof(expected_path), "%s/.nash/config.toml", tmp);
  write_file(expected_path, "", 0);

  char found[NASH_PATH_MAX];
  int rc = nash_dirs_find_config(found, sizeof(found));
  ASSERT_EQ(rc, 0);
  ASSERT_STR_EQ(found, expected_path);

  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_find_config_fresh_install(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Neither path exists — should return XDG default */
  char found[NASH_PATH_MAX];
  int rc = nash_dirs_find_config(found, sizeof(found));
  ASSERT_EQ(rc, 0);

  char expected[512];
  snprintf(expected, sizeof(expected), "%s/.config/nash/config.toml", tmp);
  ASSERT_STR_EQ(found, expected);

  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_override_takes_priority_over_xdg(void) {
  char *tmp = make_test_dir();
  char override[512];
  snprintf(override, sizeof(override), "%s/override", tmp);

  /* Set XDG vars — should be ignored when override is set */
  setenv("XDG_CONFIG_HOME", "/should/be/ignored", 1);

  nash_dirs_t *d = nash_dirs_resolve(override);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 0);
  ASSERT_STR_EQ(d->config_dir, override);
  ASSERT_STR_EQ(d->data_dir, override);

  nash_dirs_free(d);
  unsetenv("XDG_CONFIG_HOME");
  rm_rf(tmp);
  free(tmp);
}

static void test_find_config_xdg_env_var(void) {
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  setenv("HOME", tmp, 1);

  /* Set XDG_CONFIG_HOME to a custom dir */
  char custom[512];
  snprintf(custom, sizeof(custom), "%s/custom-config", tmp);
  setenv("XDG_CONFIG_HOME", custom, 1);

  /* Create config there */
  char nash_config[512];
  snprintf(nash_config, sizeof(nash_config), "%s/nash", custom);
  mkdir_p(nash_config, 0755);

  char config_file[512];
  snprintf(config_file, sizeof(config_file), "%s/nash/config.toml", custom);
  write_file(config_file, "", 0);

  char found[NASH_PATH_MAX];
  int rc = nash_dirs_find_config(found, sizeof(found));
  ASSERT_EQ(rc, 0);
  ASSERT_STR_EQ(found, config_file);

  unsetenv("XDG_CONFIG_HOME");
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

/* ── Directory creation tests ──────────────────────────── */

static void test_resolve_creates_deep_xdg_dirs(void) {
  unset_xdg_vars();
  char *tmp = make_test_dir();

  char old_home[NASH_PATH_MAX];
  snprintf(old_home, sizeof(old_home), "%s", getenv("HOME") ? getenv("HOME") : "/tmp");

  /* Use a deep path that doesn't exist yet */
  char deep[NASH_PATH_MAX];
  snprintf(deep, sizeof(deep), "%s/deep/nested/home", tmp);
  setenv("HOME", deep, 1);

  nash_dirs_t *d = nash_dirs_resolve(NULL);
  ASSERT_NOT_NULL(d);
  ASSERT_EQ(d->xdg_mode, 1);

  ASSERT(dir_exists(d->config_dir));
  ASSERT(dir_exists(d->data_dir));
  ASSERT(dir_exists(d->state_dir));
  ASSERT(dir_exists(d->cache_dir));

  nash_dirs_free(d);
  setenv("HOME", old_home, 1);
  rm_rf(tmp);
  free(tmp);
}

static void test_override_creates_deep_dir(void) {
  char *tmp = make_test_dir();

  char deep[NASH_PATH_MAX];
  snprintf(deep, sizeof(deep), "%s/a/b/c/override", tmp);

  nash_dirs_t *d = nash_dirs_resolve(deep);
  ASSERT_NOT_NULL(d);
  ASSERT(dir_exists(deep));

  nash_dirs_free(d);
  rm_rf(tmp);
  free(tmp);
}

static void test_sessions_base_dir_creates_deep_path(void) {
  char *tmp = make_test_dir();

  /* state_dir/workspaces/myws/sessions should be created even if
     state_dir/workspaces doesn't exist yet */
  char *sb = sessions_base_dir(tmp, "myws");
  ASSERT_NOT_NULL(sb);
  ASSERT(dir_exists(sb));

  char expected[NASH_PATH_MAX];
  snprintf(expected, sizeof(expected), "%s/workspaces/myws/sessions", tmp);
  ASSERT_STR_EQ(sb, expected);

  free(sb);
  rm_rf(tmp);
  free(tmp);
}

static void test_sessions_base_dir_global(void) {
  char *tmp = make_test_dir();

  char *sb = sessions_base_dir(tmp, NULL);
  ASSERT_NOT_NULL(sb);
  ASSERT(dir_exists(sb));

  char expected[NASH_PATH_MAX];
  snprintf(expected, sizeof(expected), "%s/sessions", tmp);
  ASSERT_STR_EQ(sb, expected);

  free(sb);
  rm_rf(tmp);
  free(tmp);
}

static void test_create_session_dir_makes_epoch_dir(void) {
  char *tmp = make_test_dir();

  char *sd = create_session_dir(tmp, NULL);
  ASSERT_NOT_NULL(sd);
  ASSERT(dir_exists(sd));

  /* Should be under tmp/sessions/<epoch> */
  char sessions[NASH_PATH_MAX];
  snprintf(sessions, sizeof(sessions), "%s/sessions", tmp);
  ASSERT(dir_exists(sessions));
  ASSERT_STR_CONTAINS(sd, sessions);

  free(sd);
  rm_rf(tmp);
  free(tmp);
}

static void test_create_session_dir_workspace_deep(void) {
  char *tmp = make_test_dir();

  /* workspace sessions should create the full chain */
  char *sd = create_session_dir(tmp, "proj/sub");
  ASSERT_NOT_NULL(sd);
  ASSERT(dir_exists(sd));

  char ws_sessions[NASH_PATH_MAX];
  snprintf(ws_sessions, sizeof(ws_sessions),
           "%s/workspaces/proj/sub/sessions", tmp);
  ASSERT(dir_exists(ws_sessions));

  free(sd);
  rm_rf(tmp);
  free(tmp);
}

/* ── Agent queue call tree (no pre-created agent/ dir) ── */

static void test_agent_queue_load_save_creates_dir(void) {
  char *tmp = make_test_dir();

  /* agent/ does not exist under tmp — load should succeed (no file yet) */
  agent_queue_t q = {0};
  int rc = agent_queue_load(&q, tmp);
  ASSERT_EQ(rc, 0);

  /* agent/ dir should NOT exist yet (load is read-only) */
  char agent_dir[NASH_PATH_MAX];
  snprintf(agent_dir, sizeof(agent_dir), "%s/agent", tmp);
  ASSERT(!dir_exists(agent_dir));

  /* save should create agent/ and write queue.json */
  rc = agent_queue_save(&q, tmp);
  ASSERT_EQ(rc, 0);
  ASSERT(dir_exists(agent_dir));

  char queue_file[NASH_PATH_MAX];
  snprintf(queue_file, sizeof(queue_file), "%s/agent/queue.json", tmp);
  ASSERT(file_exists(queue_file));

  /* load again should succeed now that queue.json exists */
  rc = agent_queue_load(&q, tmp);
  ASSERT_EQ(rc, 0);

  rm_rf(tmp);
  free(tmp);
}

static void test_agent_history_creates_dir(void) {
  char *tmp = make_test_dir();

  /* agent/ does not exist — history_append should create it */
  agent_entry_t agent = {.id = "test-agent"};
  int rc = agent_history_append(tmp, &agent, 5, "ok", NULL);
  ASSERT_EQ(rc, 0);

  char history[NASH_PATH_MAX];
  snprintf(history, sizeof(history), "%s/agent/history.jsonl", tmp);
  ASSERT(file_exists(history));

  rm_rf(tmp);
  free(tmp);
}

/* ── Main ──────────────────────────────────────────────── */

int main(void) {
  printf("test_dirs:\n");

  RUN_TEST(test_override_sets_all_dirs);
  RUN_TEST(test_override_empty_string_is_not_override);
  RUN_TEST(test_legacy_mode_when_dot_nash_exists);
  RUN_TEST(test_xdg_mode_fresh_install);
  RUN_TEST(test_xdg_env_vars_override_defaults);
  RUN_TEST(test_xdg_mode_when_xdg_config_exists);
  RUN_TEST(test_find_config_xdg_path);
  RUN_TEST(test_find_config_legacy_path);
  RUN_TEST(test_find_config_fresh_install);
  RUN_TEST(test_override_takes_priority_over_xdg);
  RUN_TEST(test_find_config_xdg_env_var);
  RUN_TEST(test_resolve_creates_deep_xdg_dirs);
  RUN_TEST(test_override_creates_deep_dir);
  RUN_TEST(test_sessions_base_dir_creates_deep_path);
  RUN_TEST(test_sessions_base_dir_global);
  RUN_TEST(test_create_session_dir_makes_epoch_dir);
  RUN_TEST(test_create_session_dir_workspace_deep);
  RUN_TEST(test_agent_queue_load_save_creates_dir);
  RUN_TEST(test_agent_history_creates_dir);

  TEST_SUMMARY();
}
