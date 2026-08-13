# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is nash

Nash is an agentic harness for LLMs, implemented as a single compiled C11 binary (~63k lines). It connects to OpenAI-compatible LLM servers (llama.cpp, OpenAI, Anthropic, Vertex AI) and executes coding tasks through a ReAct loop with persistent memory, an ncurses TUI, and a plugin system.

## Build & Test

The Makefile auto-detects Linux vs macOS via `uname -s`. On macOS, Homebrew paths are auto-detected via `brew --prefix`.

```bash
make                  # builds nash binary + libnash (soname-versioned)
make test             # runs all unit tests
make fmt              # clang-format all C source
make clean            # remove build artifacts
make dist             # source tarball (tar.zst)
```

Run a single test:
```bash
make tests/test_memory && ./tests/test_memory
```

Tests link against libnash (.so on Linux, .dylib on macOS) via rpath set by the Makefile.

Integration tests require a running LLM server:
```bash
./tests/run_integration.sh --api http://localhost:8080
```

### Dependencies

**Linux:** libcurl, OpenSSL (libcrypto), readline, ncursesw, pthreads, libutf8proc, libdl. ONNX Runtime is optional (for local embeddings). Podman/Docker optional (SearXNG auto-start for web_search).

**macOS (Homebrew):** `brew install gcc curl openssl readline ncurses utf8proc`. ONNX Runtime optional: `brew install onnxruntime`. Homebrew GCC is required (Apple Clang is not supported).

## Architecture

### Core loop: ReAct (react.c)

The central loop lives in `react.c` with helpers split into `react_context.c`, `react_checkpoint.c`, `react_reflection.c`, `react_error.c`, `react_eviction.c`. The loop runs on an **inference thread** while the TUI runs on the main thread. Thread ownership is documented in `react_ctx_t` (react.h) — fields are annotated INIT-ONLY, MAIN→INFER, INFER→MAIN, or BETWEEN-RUNS. Respect these annotations when modifying shared state.

### Provider abstraction (provider.h)

Vtable-based polymorphism: `provider_t` has function pointers for `build_headers`, `build_request`, `parse_response`, `parse_sse_event`, `get_endpoint`, `build_tools`. Four implementations: `provider_local.c`, `provider_openai.c`, `provider_anthropic.c` (also used for Vertex). Add new providers by implementing this vtable.

### Tool plugin system (tool_plugin.h)

Tools self-register via `__attribute__((constructor))` using `TOOL_PLUGIN_REGISTER()`. Each tool is a `tool_plugin_t` struct with name, description, parameter definitions (`tool_param_t` array), and an execute function. Parameters use `TOOL_PARAM()` / `TOOL_PARAM_END` macros — JSON Schema is generated at runtime by `tool_params_to_cjson()`.

Built-in tools are in `tool_file.c`, `tool_search.c`, `tool_notes.c`, `tool_memory.c`, `tool_web.c`, `tool_image.c`, `tool_todo.c`, `tool_subtask.c`. External plugins are shared objects (`.so` on Linux, `.dylib` on macOS) loaded via `tool_plugin_load()` / `tool_plugin_load_dir()` with ABI version checking (`TOOL_PLUGIN_ABI_VERSION`).

### Memory system (memory.h)

Four-tier architecture: context window (volatile) → scratchpad (session) → session journal (searchable) → curated memory (git-backed, Bayesian-validated). The `memory_t` struct uses an in-memory index (`mem_index_t`) with O(1) hash map lookup. Thread safety via `pthread_mutex_t`. Memory entries have Bayesian validation scores (recall_hits/misses), embeddings, inter-memory refs, and lineage tracking (supersedes/version).

### Context management (react_eviction.c)

Messages are typed (`llm_msg_type_t`) with importance levels (LOW→CRITICAL) and recoverability tags. Eviction follows: recoverable content first, then compress via sentence-BM25, then evict low-importance. The `llm_chat_t` tracks `total_chars` incrementally — use `llm_chat_replace_content()` instead of direct field mutation.

### TUI (frontend_tui.c, tui.c, ui_*.c)

ncurses-based with markdown rendering (`md_render.c`). The TUI thread polls for `react_event_t` events from the inference thread. UI state is managed in `ui_state.c` with navigation in `ui_nav.c` and event handling in `ui_event.c`.

### Configuration (config.h, config.c, toml.c)

TOML-based config at `~/.nash/config.toml`. Model profiles in `~/.nash/models/*.toml` provide per-model overrides. Config fields use sentinel values (-1, 0.0, NULL) to mean "inherit" — check the SENTINEL REFERENCE comment in `config.h` before adding fields.

## Code style

- C11 with LLVM-based clang-format (`.clang-format` at repo root)
- 2-space indentation, no tabs, no column limit
- Pointer alignment right (`char *foo`)
- All `.o` files depend on all headers (conservative rebuild)
- `cJSON.c`/`cJSON.h` is vendored — do not modify

## Test framework

Tests use a custom lightweight framework in `tests/test_common.h`. Each test file has a `main()` that calls `RUN_TEST(name)` for each test function, ending with `TEST_SUMMARY()`. Assertions: `ASSERT()`, `ASSERT_EQ()`, `ASSERT_STR_EQ()`, `ASSERT_STR_CONTAINS()`, `ASSERT_NOT_NULL()`, `ASSERT_NULL()`. Tests create temp dirs via `make_test_dir()` and clean up with `rm_rf()`.

## Key patterns

- **tool_result_t**: All tool handlers return `{meta, store_ref, success, importance}`. Use `tools_make_result()` / `tools_make_error()` helpers.
- **Deferred consolidation**: Memory consolidations queue during react loop, flush after task completion (`tool_flush_deferred_consolidations()`).
- **Fire ledger**: Dedup memory injection within a context window, resets on compaction.
- **Step aliases**: Tool results get aliases like `R1S0` (react loop 1, step 0) via `tool_register_alias()`.
- **Playbook tool filters**: YAML playbooks can whitelist/blacklist tools per pass via `tool_filter_t`.
