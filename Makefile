VERSION ?= 0.1.2

# ── Platform detection ────────────────────────────────────────
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
  CC      ?= gcc
  PLATFORM_DEFINES = -D_DEFAULT_SOURCE
  NCURSES_LIB      = -lncursesw
  DL_LIB           = -ldl
  SHARED_EXT       = so
  SHARED_FLAG      = -shared
  SONAME_FLAG      = -Wl,-soname,$(LIB_SONAME)
  RPATH_ORIGIN     = $$ORIGIN
  EXPORT_DYNAMIC   = -rdynamic
  PLUGIN_EXT       = so
  BREW_CFLAGS      =
  BREW_LDFLAGS     =
else ifeq ($(UNAME_S),Darwin)
  # Homebrew GCC is required — Apple Clang is not supported.
  # Auto-detect the highest-versioned gcc-NN in Homebrew's gcc prefix.
  BREW_GCC_PREFIX := $(shell brew --prefix gcc 2>/dev/null)
  ifeq ($(BREW_GCC_PREFIX),)
    $(error Homebrew GCC not found — install with: brew install gcc)
  endif
  CC := $(shell ls $(BREW_GCC_PREFIX)/bin/gcc-[0-9][0-9] 2>/dev/null | sort -V | tail -1)
  ifeq ($(CC),)
    $(error No gcc-NN binary found in $(BREW_GCC_PREFIX)/bin/)
  endif
  PLATFORM_DEFINES = -D_DARWIN_C_SOURCE
  NCURSES_LIB      = -lncurses
  DL_LIB           =
  SHARED_EXT       = dylib
  SHARED_FLAG      = -dynamiclib
  RPATH_ORIGIN     = @loader_path
  # Plugins link against libnash directly (-L. -lnash), so -rdynamic is not needed
  EXPORT_DYNAMIC   =
  PLUGIN_EXT       = dylib
  # Auto-detect Homebrew paths for keg-only packages
  BREW_PREFIX      := $(shell brew --prefix 2>/dev/null)
  ifneq ($(BREW_PREFIX),)
    BREW_NCURSES   := $(shell brew --prefix ncurses 2>/dev/null)
    BREW_READLINE  := $(shell brew --prefix readline 2>/dev/null)
    BREW_OPENSSL   := $(shell brew --prefix openssl 2>/dev/null)
    BREW_UTF8PROC  := $(shell brew --prefix utf8proc 2>/dev/null)
    BREW_ONNX     := $(shell brew --prefix onnxruntime 2>/dev/null)
    BREW_CFLAGS    = $(if $(BREW_NCURSES),-I$(BREW_NCURSES)/include) \
                     $(if $(BREW_READLINE),-I$(BREW_READLINE)/include) \
                     $(if $(BREW_OPENSSL),-I$(BREW_OPENSSL)/include) \
                     $(if $(BREW_UTF8PROC),-I$(BREW_UTF8PROC)/include) \
                     $(if $(BREW_ONNX),-I$(BREW_ONNX)/include)
    BREW_LDFLAGS   = $(if $(BREW_NCURSES),-L$(BREW_NCURSES)/lib) \
                     $(if $(BREW_READLINE),-L$(BREW_READLINE)/lib) \
                     $(if $(BREW_OPENSSL),-L$(BREW_OPENSSL)/lib) \
                     $(if $(BREW_UTF8PROC),-L$(BREW_UTF8PROC)/lib) \
                     $(if $(BREW_ONNX),-L$(BREW_ONNX)/lib)
  else
    BREW_CFLAGS    =
    BREW_LDFLAGS   =
  endif
else
  $(error Unsupported platform: $(UNAME_S) — requires Linux or Darwin)
endif

# Library names: Linux uses libnash.so.X.Y.Z, macOS uses libnash.X.Y.Z.dylib
ifeq ($(UNAME_S),Linux)
  LIB_REAL   = libnash.$(SHARED_EXT).$(VERSION)
  LIB_SONAME = libnash.$(SHARED_EXT).0
  LIB_LINKER = libnash.$(SHARED_EXT)
  SONAME_FLAG = -Wl,-soname,$(LIB_SONAME)
else ifeq ($(UNAME_S),Darwin)
  LIB_REAL   = libnash.$(VERSION).$(SHARED_EXT)
  LIB_SONAME = libnash.0.$(SHARED_EXT)
  LIB_LINKER = libnash.$(SHARED_EXT)
  SONAME_FLAG = -Wl,-install_name,@rpath/$(LIB_LINKER)
endif

RPATH_FLAG        = -Wl,-rpath,'$(RPATH_ORIGIN)'
RPATH_PARENT_FLAG = -Wl,-rpath,'$(RPATH_ORIGIN)/..'
# ── End platform detection ────────────────────────────────────

CFLAGS  ?= -Wall -g -Wextra -Wunused-function -O2 -std=c11 -fPIC -D_POSIX_C_SOURCE=200809L $(PLATFORM_DEFINES)
CFLAGS  += $(BREW_CFLAGS)
# ONNX Runtime: requires onnxruntime-devel (headers) to build.
# For linking, use pip-installed libonnxruntime if no system package.
ORT_LIB := $(shell python3 -c "import onnxruntime; import os; print(os.path.dirname(onnxruntime.__file__) + '/capi')" 2>/dev/null)
ifneq ($(ORT_LIB),)
  ORT_LDFLAGS = -L$(ORT_LIB) -Wl,-rpath,$(ORT_LIB) -lonnxruntime
else
  ORT_LDFLAGS = -lonnxruntime
endif

# Device subsystem (VNC, HEVC streaming, Tesseract OCR) is now a separate
# plugin: nash-tool-device-control.  See ~/agents/nash-tool-device-control/
LDFLAGS ?= $(EXPORT_DYNAMIC) -lcurl -lcrypto -lreadline $(NCURSES_LIB) -lpthread -lm -lutf8proc $(DL_LIB) $(BREW_LDFLAGS) $(ORT_LDFLAGS)

# Default data directory (playbooks, etc.) -- /usr/share/nash for installed builds
NASH_DATADIR ?= /usr/share/nash
CFLAGS  += -DNASH_DATADIR='"$(NASH_DATADIR)"'

SRC     = src/main.c src/str.c src/cJSON.c \
          src/journal.c src/store.c src/llm.c src/tools.c src/react.c \
          src/react_context.c \
          src/react_checkpoint.c src/react_reflection.c \
          src/react_error.c src/react_eviction.c \
          src/config.c src/toml.c \
          src/provider.c src/provider_local.c \
          src/provider_openai.c src/provider_anthropic.c \
          src/frontend_tui.c \
          src/ui_state.c src/ui_md_gen.c src/ui_nav.c src/ui_event.c \
          src/tui.c src/md_render.c \
          src/md_diff.c src/md_osc8.c \
          src/memory.c src/mem_git.c \
          src/workspace.c \
          src/embedding.c \
          src/embedding_onnx.c \
          src/nash_log.c \
          src/yaml_parse.c \
          src/playbook.c \
          src/regression.c \
          src/postmortem.c \
          src/prompt_optimize.c \
          src/scratchpad.c \
          src/session_index.c \
          src/tool_file.c \
          src/tool_search.c \
          src/tool_notes.c \
          src/tool_memory.c \
          src/tool_web.c \
          src/tool_image.c \
          src/tool_todo.c \
          src/todo_core.c \
          src/session_search.c \
          src/mailbox.c \
          src/telegram.c \
          src/md_html.c \
          src/matrix.c \
          src/compress.c \
          src/html_extract.c \
          src/searxng.c \
          src/banner.c \
          src/commands.c \
          src/cmd_todo.c \
          src/cmd_agents.c \
          src/cmd_tool.c \
          src/agents.c \
          src/repomap.c \
          src/tool_subtask.c \
          src/completion.c \
          src/subprocess.c \
          src/tool_plugin.c \
          src/fswatch.c \
          src/setup.c
OBJ     = $(SRC:.c=.o)
BIN     = nash

all: $(LIB_REAL) $(BIN)

# Header dependencies -- ALL .o files depend on ALL headers.
# This is conservative but safe: changing any header recompiles everything.
# For a 15-file project this adds <1s to rebuilds.
HDRS    = $(wildcard src/*.h)

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Library objects (everything except main.c for linking with tests)
LIB_SRC = src/str.c src/cJSON.c src/journal.c src/store.c \
          src/llm.c src/tools.c src/react.c src/react_context.c \
          src/react_checkpoint.c \
          src/react_reflection.c \
          src/react_error.c src/react_eviction.c \
          src/config.c src/toml.c \
          src/provider.c src/provider_local.c \
          src/provider_openai.c src/provider_anthropic.c \
          src/frontend_tui.c src/ui_state.c src/ui_md_gen.c src/ui_nav.c src/ui_event.c \
          src/tui.c src/md_render.c src/memory.c src/mem_git.c \
          src/md_diff.c src/md_osc8.c \
          src/workspace.c \
          src/embedding.c src/embedding_onnx.c \
          src/nash_log.c \
          src/yaml_parse.c src/playbook.c \
          src/regression.c src/postmortem.c \
          src/prompt_optimize.c \
          src/scratchpad.c \
          src/session_index.c \
          src/tool_file.c \
          src/tool_search.c \
          src/tool_notes.c \
          src/tool_memory.c \
          src/tool_web.c \
          src/tool_image.c \
          src/tool_todo.c \
          src/todo_core.c \
          src/session_search.c \
          src/mailbox.c \
          src/telegram.c \
          src/md_html.c \
          src/matrix.c \
          src/compress.c \
          src/html_extract.c \
          src/searxng.c \
          src/banner.c \
          src/commands.c \
          src/cmd_todo.c \
          src/cmd_agents.c \
          src/cmd_tool.c \
          src/agents.c \
          src/repomap.c \
          src/tool_subtask.c \
          src/completion.c \
          src/subprocess.c \
          src/tool_plugin.c \
          src/fswatch.c \
          src/setup.c
LIB_OBJ = $(LIB_SRC:.c=.o)

# Shared library: everything except main.c
$(LIB_REAL): $(LIB_OBJ)
	$(CC) $(SHARED_FLAG) $(SONAME_FLAG) -o $@ $^ $(LDFLAGS)
	ln -sf $(LIB_REAL) $(LIB_SONAME)
	ln -sf $(LIB_SONAME) $(LIB_LINKER)

# Binary: main.o links against libnash
$(BIN): src/main.o $(LIB_REAL)
	$(CC) $(CFLAGS) -o $@ $< -L. -lnash $(RPATH_FLAG) $(LDFLAGS)

# Test binaries
TEST_BIN = tests/test_memory tests/test_store tests/test_config \
           tests/test_str tests/test_journal tests/test_memory_context \
           tests/test_spec tests/test_compaction \
           tests/test_lifecycle tests/test_memory_query \
           tests/test_compress tests/test_semantic_scoring \
           tests/test_breadcrumbs tests/test_optimizer \
           tests/test_reflection tests/test_tool_plugin \
           tests/test_tool_plugin_dlopen

# Sample plugin shared objects for dlopen testing
SAMPLE_PLUGINS = tests/sample_plugin.$(PLUGIN_EXT) tests/sample_plugin_bad_abi.$(PLUGIN_EXT) \
                 tests/sample_plugin_multi.$(PLUGIN_EXT)

tests/sample_%.$(PLUGIN_EXT): tests/sample_%.c src/tool_plugin.h src/cJSON.h $(LIB_REAL)
	$(CC) $(SHARED_FLAG) -fPIC $(CFLAGS) -I src -o $@ $< -L. -lnash

# dlopen test depends on sample plugin files
tests/test_tool_plugin_dlopen: tests/test_tool_plugin_dlopen.c $(LIB_REAL) $(SAMPLE_PLUGINS)
	$(CC) $(CFLAGS) -I src -o $@ $< -L. -lnash $(RPATH_PARENT_FLAG) $(LDFLAGS)

tests/test_%: tests/test_%.c $(LIB_REAL)
	$(CC) $(CFLAGS) -I src -o $@ $< -L. -lnash $(RPATH_PARENT_FLAG) $(LDFLAGS)

test: $(TEST_BIN)
	@echo "=== Running tests ==="
	@failures=0; \
	for t in $(TEST_BIN); do \
		echo "--- $$t ---"; \
		if ./$$t; then echo "PASS"; else echo "FAIL"; failures=$$((failures+1)); fi; \
	done; \
	echo "=== $$failures failures ==="

clean:
	rm -f $(OBJ) $(BIN) $(LIB_REAL) $(LIB_SONAME) $(LIB_LINKER) $(TEST_BIN) $(SAMPLE_PLUGINS)
	rm -f libnash.so* libnash*.dylib
	rm -rf tests/plugin_dir tests/*.dSYM

# Source tarball for RPM builds (matches spec Source0: nash-VERSION.tar.zst)
dist:
	git archive --format=tar --prefix=nash-$(VERSION)/ HEAD | zstd -o nash-$(VERSION).tar.zst

# Format all C source files
fmt:
	git ls-files -z '*.c' '*.h' | xargs -0 clang-format -i

test-container:
	@if podman container exists nash 2>/dev/null; then \
	  podman start nash 2>/dev/null || true; \
	  podman exec nash bash -c "make clean && make && make test"; \
	else \
	  podman run --name nash -d \
	    -v $(CURDIR):/workspace:Z \
	    -w /workspace \
	    registry.fedoraproject.org/fedora:latest \
	    sleep infinity; \
	  podman exec nash bash -c "dnf install -y gcc make libcurl-devel openssl-devel readline-devel ncurses-devel utf8proc-devel onnxruntime-devel"; \
	  podman exec nash bash -c "make clean && make && make test"; \
	fi
	$(MAKE) clean

.PHONY: all clean test test-container dist fmt
