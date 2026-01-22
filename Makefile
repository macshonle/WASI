# Makefile for generating WASI C bindings
# Target: UNIX/macOS with GNU bash 3.2.57+
#
# This Makefile generates C bindings from WASI WIT (WebAssembly Interface Types)
# files using wit-bindgen. The generated bindings can be used to implement
# the WebAssembly System Interface in C.
#
# Prerequisites:
#   cargo install wasm-tools wit-deps-cli wit-bindgen-cli
#
# Usage:
#   make              - Generate all C bindings (uses current WIT files)
#   make v0.2.0       - Checkout v0.2.0 tag and generate bindings from that version
#   make clean        - Remove generated files
#   make deps         - Fetch WIT dependencies only
#   make help         - Show this help
#
# Note: WASI v0.2.0 uses a different directory structure (preview2/) than
# later versions (proposals/*/wit/). This Makefile handles both.

SHELL := /bin/bash

# Configuration
BUILD_DIR := build
BINDINGS_DIR := $(BUILD_DIR)/c-bindings
WIT_CACHE_DIR := $(BUILD_DIR)/wit-cache

# Proposals and their worlds (for current repo structure)
# Format: proposal:world
PROPOSALS := \
	io:imports \
	random:imports \
	clocks:imports \
	filesystem:imports \
	sockets:imports \
	cli:imports \
	cli:command \
	http:imports \
	http:proxy

# Proposals for v0.2.0 (preview2 structure, different worlds available)
# v0.2.0 has: cli, clocks, filesystem, http, io, random, sockets
PROPOSALS_V020 := \
	io:imports \
	random:imports \
	clocks:imports \
	filesystem:imports \
	sockets:imports \
	cli:imports \
	cli:command \
	http:proxy

# Proposals that require wit-deps to fetch dependencies (modern structure only)
WITH_DEPS_PROPOSALS := clocks filesystem sockets cli http

# Tools
WIT_BINDGEN := wit-bindgen
WIT_DEPS := wit-deps
WASM_TOOLS := wasm-tools
GIT := git

# Check for required tools
.PHONY: check-tools
check-tools:
	@command -v $(WIT_BINDGEN) >/dev/null 2>&1 || { echo "Error: wit-bindgen not found. Install with: cargo install wit-bindgen-cli"; exit 1; }
	@command -v $(WASM_TOOLS) >/dev/null 2>&1 || { echo "Error: wasm-tools not found. Install with: cargo install wasm-tools"; exit 1; }

.PHONY: check-tools-with-deps
check-tools-with-deps: check-tools
	@command -v $(WIT_DEPS) >/dev/null 2>&1 || { echo "Error: wit-deps not found. Install with: cargo install wit-deps-cli"; exit 1; }

# Default target: generate bindings from current WIT files
.PHONY: all
all: check-tools-with-deps $(BINDINGS_DIR) deps bindings
	@echo ""
	@echo "C bindings generated in $(BINDINGS_DIR)/"
	@echo "Generated files:"
	@find $(BINDINGS_DIR) -name '*.h' -o -name '*.c' | sort

# ============================================================================
# Version-specific targets
# ============================================================================

# v0.2.0 uses the old preview2/ structure (no deps.toml, different layout)
# All directories must be passed together in dependency order
.PHONY: v0.2.0
v0.2.0: check-tools
	@echo "Generating C bindings for WASI v0.2.0..."
	@rm -rf $(WIT_CACHE_DIR)
	@mkdir -p $(WIT_CACHE_DIR)
	@echo "Checking out WASI v0.2.0..."
	@$(GIT) archive --format=tar v0.2.0 preview2/ | tar -xf - -C $(WIT_CACHE_DIR)
	@mkdir -p $(BINDINGS_DIR)
	@# For v0.2.0, all WIT dirs must be passed together in dependency order
	@# Dependency order: io, random, clocks, filesystem, sockets, cli, http
	$(eval V020_WIT_DIRS := $(WIT_CACHE_DIR)/preview2/io \
		$(WIT_CACHE_DIR)/preview2/random \
		$(WIT_CACHE_DIR)/preview2/clocks \
		$(WIT_CACHE_DIR)/preview2/filesystem \
		$(WIT_CACHE_DIR)/preview2/sockets \
		$(WIT_CACHE_DIR)/preview2/cli \
		$(WIT_CACHE_DIR)/preview2/http)
	@echo "Generating C bindings from v0.2.0 (preview2 structure)..."
	@# Generate individual world bindings
	@echo "  Generating io (wasi:io/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/io
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:io/imports@0.2.0 --out-dir $(BINDINGS_DIR)/io
	@echo "  Generating random (wasi:random/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/random
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:random/imports@0.2.0 --out-dir $(BINDINGS_DIR)/random
	@echo "  Generating clocks (wasi:clocks/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/clocks
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:clocks/imports@0.2.0 --out-dir $(BINDINGS_DIR)/clocks
	@echo "  Generating filesystem (wasi:filesystem/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/filesystem
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:filesystem/imports@0.2.0 --out-dir $(BINDINGS_DIR)/filesystem
	@echo "  Generating sockets (wasi:sockets/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/sockets
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:sockets/imports@0.2.0 --out-dir $(BINDINGS_DIR)/sockets
	@echo "  Generating cli/imports (wasi:cli/imports@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/cli
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:cli/imports@0.2.0 --out-dir $(BINDINGS_DIR)/cli --rename-world cli-imports
	@echo "  Generating cli/command (wasi:cli/command@0.2.0)..."
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:cli/command@0.2.0 --out-dir $(BINDINGS_DIR)/cli
	@echo "  Generating http (wasi:http/proxy@0.2.0)..."
	@mkdir -p $(BINDINGS_DIR)/http
	@$(WIT_BINDGEN) c $(V020_WIT_DIRS) -w wasi:http/proxy@0.2.0 --out-dir $(BINDINGS_DIR)/http
	@echo ""
	@echo "C bindings for WASI v0.2.0 generated in $(BINDINGS_DIR)/"
	@echo "Generated files:"
	@find $(BINDINGS_DIR) -name '*.h' -o -name '*.c' | sort

# v0.2.1+ use the modern proposals/*/wit/ structure with deps.toml
.PHONY: v0.2.1 v0.2.2 v0.2.3 v0.2.4 v0.2.5 v0.2.6 v0.2.7 v0.2.8 v0.2.9
v0.2.1 v0.2.2 v0.2.3 v0.2.4 v0.2.5 v0.2.6 v0.2.7 v0.2.8 v0.2.9: check-tools-with-deps
	@echo "Generating C bindings for WASI $@..."
	@rm -rf $(WIT_CACHE_DIR)
	@mkdir -p $(WIT_CACHE_DIR)
	@echo "Checking out WASI $@..."
	@$(GIT) archive --format=tar $@ proposals/ | tar -xf - -C $(WIT_CACHE_DIR)
	@echo "Fetching WIT dependencies..."
	@for proposal in $(WITH_DEPS_PROPOSALS); do \
		if [ -f "$(WIT_CACHE_DIR)/proposals/$$proposal/wit/deps.toml" ]; then \
			echo "  Fetching deps for $$proposal..."; \
			$(WIT_DEPS) -m "$(WIT_CACHE_DIR)/proposals/$$proposal/wit/deps.toml" \
				-l "$(WIT_CACHE_DIR)/proposals/$$proposal/wit/deps.lock" \
				-d "$(WIT_CACHE_DIR)/proposals/$$proposal/wit/deps" update || exit 1; \
		fi; \
	done
	@mkdir -p $(BINDINGS_DIR)
	@echo "Generating C bindings..."
	@for pw in $(PROPOSALS); do \
		proposal=$${pw%%:*}; \
		world=$${pw##*:}; \
		outdir="$(BINDINGS_DIR)/$$proposal"; \
		mkdir -p "$$outdir"; \
		echo "  Generating $$proposal (world: $$world)..."; \
		$(WIT_BINDGEN) c "$(WIT_CACHE_DIR)/proposals/$$proposal/wit" \
			-w "$$world" \
			--out-dir "$$outdir" 2>&1 || exit 1; \
	done
	@echo ""
	@echo "C bindings for WASI $@ generated in $(BINDINGS_DIR)/"
	@echo "Generated files:"
	@find $(BINDINGS_DIR) -name '*.h' -o -name '*.c' | sort

# ============================================================================
# Working with current repo (HEAD)
# ============================================================================

# Create output directories
$(BINDINGS_DIR):
	@mkdir -p $(BINDINGS_DIR)

# Fetch dependencies for all proposals (from main repo)
.PHONY: deps
deps:
	@echo "Fetching WIT dependencies..."
	@for proposal in $(WITH_DEPS_PROPOSALS); do \
		if [ -f "proposals/$$proposal/wit/deps.toml" ]; then \
			echo "  Fetching deps for $$proposal..."; \
			$(WIT_DEPS) -m "proposals/$$proposal/wit/deps.toml" \
				-l "proposals/$$proposal/wit/deps.lock" \
				-d "proposals/$$proposal/wit/deps" update || exit 1; \
		fi; \
	done
	@echo "Dependencies fetched."

# Generate all C bindings (from main repo)
.PHONY: bindings
bindings: $(BINDINGS_DIR)
	@echo "Generating C bindings..."
	@for pw in $(PROPOSALS); do \
		proposal=$${pw%%:*}; \
		world=$${pw##*:}; \
		outdir="$(BINDINGS_DIR)/$$proposal"; \
		mkdir -p "$$outdir"; \
		echo "  Generating $$proposal (world: $$world)..."; \
		$(WIT_BINDGEN) c "proposals/$$proposal/wit" \
			-w "$$world" \
			--out-dir "$$outdir" 2>&1 || exit 1; \
	done
	@echo "Bindings generated."

# Generate bindings for a single proposal
# Usage: make proposal-io proposal-cli etc.
.PHONY: proposal-%
proposal-%: check-tools-with-deps $(BINDINGS_DIR)
	@proposal=$*; \
	world="imports"; \
	if [ "$$proposal" = "cli" ]; then world="command"; fi; \
	if [ "$$proposal" = "http" ]; then world="proxy"; fi; \
	outdir="$(BINDINGS_DIR)/$$proposal"; \
	mkdir -p "$$outdir"; \
	echo "Generating $$proposal (world: $$world)..."; \
	if echo "$(WITH_DEPS_PROPOSALS)" | grep -qw "$$proposal"; then \
		echo "  Fetching dependencies..."; \
		$(WIT_DEPS) -m "proposals/$$proposal/wit/deps.toml" \
			-l "proposals/$$proposal/wit/deps.lock" \
			-d "proposals/$$proposal/wit/deps" update; \
	fi; \
	$(WIT_BINDGEN) c "proposals/$$proposal/wit" \
		-w "$$world" \
		--out-dir "$$outdir"

# ============================================================================
# Validation
# ============================================================================

# Validate WIT files
.PHONY: validate
validate: deps
	@echo "Validating WIT files..."
	@for proposal in io random clocks filesystem sockets cli http; do \
		echo "  Validating $$proposal..."; \
		$(WASM_TOOLS) component wit "proposals/$$proposal/wit" -o /dev/null || exit 1; \
	done
	@echo "All WIT files valid."

# ============================================================================
# Cleanup
# ============================================================================

# Clean generated files
.PHONY: clean
clean:
	@echo "Cleaning generated files..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# Clean only the WIT cache (keeps bindings)
.PHONY: clean-wit-cache
clean-wit-cache:
	@rm -rf $(WIT_CACHE_DIR)

# Clean dependencies fetched by wit-deps
.PHONY: clean-deps
clean-deps:
	@echo "Cleaning fetched dependencies..."
	@for proposal in $(WITH_DEPS_PROPOSALS); do \
		rm -rf "proposals/$$proposal/wit/deps"; \
	done
	@echo "Dependencies cleaned."

# ============================================================================
# Help
# ============================================================================

.PHONY: help
help:
	@echo "WASI C Bindings Generator"
	@echo ""
	@echo "Prerequisites:"
	@echo "  cargo install wasm-tools wit-deps-cli wit-bindgen-cli"
	@echo ""
	@echo "Targets:"
	@echo "  all               - Generate C bindings from current WIT files (default)"
	@echo "  v0.2.0            - Generate bindings from WASI v0.2.0"
	@echo "  v0.2.X            - Generate bindings from any released version (0.2.1-0.2.9)"
	@echo "  deps              - Fetch WIT dependencies only"
	@echo "  validate          - Validate all WIT files"
	@echo "  proposal-X        - Generate bindings for a single proposal (e.g., proposal-io)"
	@echo "  compile           - Compile WASI C implementation (requires bindings)"
	@echo "  test              - Build and run all tests"
	@echo "  test-io           - Run only I/O tests"
	@echo "  test-random       - Run only random tests"
	@echo "  test-safe         - Build and run tests in safe mode (non-destructive only)"
	@echo "  test-safe-compare - Compare regular vs safe mode test results"
	@echo "  check-bindings    - Verify generated bindings compile"
	@echo "  clean             - Remove all generated files"
	@echo "  clean-deps        - Remove fetched WIT dependencies"
	@echo "  help              - Show this help"
	@echo ""
	@echo "Output:"
	@echo "  $(BINDINGS_DIR)/       - Generated C bindings"
	@echo "    io/               - wasi:io interfaces (streams, poll, error)"
	@echo "    random/           - wasi:random interfaces"
	@echo "    clocks/           - wasi:clocks interfaces (monotonic, wall)"
	@echo "    filesystem/       - wasi:filesystem interfaces"
	@echo "    sockets/          - wasi:sockets interfaces (tcp, udp)"
	@echo "    cli/              - wasi:cli interfaces (stdin, stdout, env, args)"
	@echo "    http/             - wasi:http interfaces (client, server)"
	@echo ""
	@echo "Example:"
	@echo "  make v0.2.0           # Generate v0.2.0 bindings"
	@echo "  make proposal-io      # Generate just the io bindings"
	@echo ""
	@echo "Note: v0.2.0 uses the old 'preview2/' structure. v0.2.1+ use the"
	@echo "      modern 'proposals/*/wit/' structure with wit-deps."

# List generated files
.PHONY: list
list:
	@if [ -d "$(BINDINGS_DIR)" ]; then \
		echo "Generated files in $(BINDINGS_DIR):"; \
		find $(BINDINGS_DIR) -type f | sort; \
	else \
		echo "No bindings generated yet. Run 'make' first."; \
	fi

# ============================================================================
# C Implementation Build (for later use)
# ============================================================================

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -I$(BINDINGS_DIR)
CFLAGS_DEBUG := $(CFLAGS) -g -O0 -DDEBUG
CFLAGS_RELEASE := $(CFLAGS) -O2 -DNDEBUG
# Safe mode: non-destructive operations only (for sandboxed testing)
CFLAGS_SAFE := $(CFLAGS_DEBUG) -DWASI_SAFE_MODE

# Source directories
SRC_DIR := src/wasi
PLATFORM_DIR := $(SRC_DIR)/platform
TEST_DIR := tests

# Object directory
OBJ_DIR := $(BUILD_DIR)/obj

# Platform detection for source file selection and WASI SDK path
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM_SRC := $(PLATFORM_DIR)/linux.c
    WASI_SDK_PATH_DEFAULT := /opt/wasi-sdk-25.0-x86_64-linux
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM_SRC := $(PLATFORM_DIR)/darwin.c
    WASI_SDK_PATH_DEFAULT := /opt/wasi-sdk-25.0-x86_64-macos
endif

# Source files
WASI_SRCS := $(wildcard $(SRC_DIR)/*.c)
PLATFORM_SRCS := $(PLATFORM_SRC)
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)

# Object files
WASI_OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(WASI_SRCS))
PLATFORM_OBJS := $(patsubst $(PLATFORM_DIR)/%.c,$(OBJ_DIR)/platform_%.o,$(PLATFORM_SRCS))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c,$(OBJ_DIR)/test_%.o,$(TEST_SRCS))

# Test binary
TEST_BIN := $(BUILD_DIR)/test_wasi

# Ensure bindings are generated before compiling
.PHONY: compile
compile: v0.2.0 $(OBJ_DIR) $(WASI_OBJS) $(PLATFORM_OBJS)
	@echo "Compiled WASI implementation objects."

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# Compile WASI implementation sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS_DEBUG) -c $< -o $@

# Compile platform sources
$(OBJ_DIR)/platform_%.o: $(PLATFORM_DIR)/%.c | $(OBJ_DIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS_DEBUG) -c $< -o $@

# Compile test sources (with TEST_RUNNER_MODE to disable individual main functions)
$(OBJ_DIR)/test_%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	@echo "  CC $<"
	@$(CC) $(CFLAGS_DEBUG) -DTEST_RUNNER_MODE -c $< -o $@

# Build and run tests
.PHONY: test
test: v0.2.0 $(OBJ_DIR) $(TEST_OBJS) $(WASI_OBJS) $(PLATFORM_OBJS)
	@echo "Linking test binary..."
	@$(CC) $(CFLAGS_DEBUG) -o $(TEST_BIN) $(TEST_OBJS) $(WASI_OBJS) $(PLATFORM_OBJS)
	@echo "Running tests..."
	@$(TEST_BIN)

# Run individual test suites
.PHONY: test-io test-random test-clocks test-cli test-filesystem test-sockets
test-io: test
	@$(TEST_BIN) io

test-random: test
	@$(TEST_BIN) random

test-clocks: test
	@$(TEST_BIN) clocks

test-cli: test
	@$(TEST_BIN) cli

test-filesystem: test
	@$(TEST_BIN) filesystem

test-sockets: test
	@$(TEST_BIN) sockets

# ============================================================================
# Safe Mode Tests (non-destructive operations only)
# ============================================================================

# Safe mode object directory
SAFE_OBJ_DIR := $(BUILD_DIR)/obj-safe

# Safe mode test binary
TEST_BIN_SAFE := $(BUILD_DIR)/test_wasi_safe

# Safe mode object files
WASI_OBJS_SAFE := $(patsubst $(SRC_DIR)/%.c,$(SAFE_OBJ_DIR)/%.o,$(WASI_SRCS))
PLATFORM_OBJS_SAFE := $(patsubst $(PLATFORM_DIR)/%.c,$(SAFE_OBJ_DIR)/platform_%.o,$(PLATFORM_SRCS))
TEST_OBJS_SAFE := $(patsubst $(TEST_DIR)/%.c,$(SAFE_OBJ_DIR)/test_%.o,$(TEST_SRCS))

$(SAFE_OBJ_DIR):
	@mkdir -p $(SAFE_OBJ_DIR)

# Compile WASI implementation sources with safe mode
$(SAFE_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(SAFE_OBJ_DIR)
	@echo "  CC [safe] $<"
	@$(CC) $(CFLAGS_SAFE) -c $< -o $@

# Compile platform sources with safe mode
$(SAFE_OBJ_DIR)/platform_%.o: $(PLATFORM_DIR)/%.c | $(SAFE_OBJ_DIR)
	@echo "  CC [safe] $<"
	@$(CC) $(CFLAGS_SAFE) -c $< -o $@

# Compile test sources with safe mode
$(SAFE_OBJ_DIR)/test_%.o: $(TEST_DIR)/%.c | $(SAFE_OBJ_DIR)
	@echo "  CC [safe] $<"
	@$(CC) $(CFLAGS_SAFE) -DTEST_RUNNER_MODE -c $< -o $@

# Build and run tests in safe mode
.PHONY: test-safe
test-safe: v0.2.0 $(SAFE_OBJ_DIR) $(TEST_OBJS_SAFE) $(WASI_OBJS_SAFE) $(PLATFORM_OBJS_SAFE)
	@echo "Linking safe mode test binary..."
	@$(CC) $(CFLAGS_SAFE) -o $(TEST_BIN_SAFE) $(TEST_OBJS_SAFE) $(WASI_OBJS_SAFE) $(PLATFORM_OBJS_SAFE)
	@echo "Running tests in SAFE MODE (non-destructive operations only)..."
	@echo "Note: Tests requiring destructive filesystem operations will fail."
	@echo ""
	@$(TEST_BIN_SAFE) || true

# Compare regular vs safe mode test results
.PHONY: test-safe-compare
test-safe-compare: test test-safe
	@echo ""
	@echo "==============================================="
	@echo "Safe Mode Comparison Report"
	@echo "==============================================="
	@echo ""
	@echo "Running regular tests..."
	@$(TEST_BIN) 2>&1 | tee $(BUILD_DIR)/test_regular.log || true
	@echo ""
	@echo "Running safe mode tests..."
	@$(TEST_BIN_SAFE) 2>&1 | tee $(BUILD_DIR)/test_safe.log || true
	@echo ""
	@echo "==============================================="
	@echo "Results Comparison"
	@echo "==============================================="
	@echo ""
	@echo "Regular mode results:"
	@grep -E "passed:|failed:" $(BUILD_DIR)/test_regular.log || true
	@echo ""
	@echo "Safe mode results:"
	@grep -E "passed:|failed:" $(BUILD_DIR)/test_safe.log || true
	@echo ""
	@echo "Differences (tests expected to fail in safe mode):"
	@diff $(BUILD_DIR)/test_regular.log $(BUILD_DIR)/test_safe.log 2>/dev/null || echo "See above for differences"

# ============================================================================
# WASI Comparison Tests (Native vs Wasmtime)
# ============================================================================

WASI_SDK_PATH ?= $(WASI_SDK_PATH_DEFAULT)
WASMTIME ?= $(HOME)/.wasmtime/bin/wasmtime
CC_WASI := $(WASI_SDK_PATH)/bin/wasm32-wasip2-clang

COMPARISON_DIR := $(TEST_DIR)/wasm-comparison
COMPARISON_BUILD := $(BUILD_DIR)/wasm-comparison
COMPARISON_NATIVE := $(COMPARISON_BUILD)/native
COMPARISON_WASI := $(COMPARISON_BUILD)/wasi

COMPARISON_SRCS := test_random.c test_clocks.c test_filesystem.c test_env.c test_details.c
COMPARISON_NATIVE_BINS := $(patsubst %.c,$(COMPARISON_NATIVE)/%,$(COMPARISON_SRCS))
COMPARISON_WASI_WASMS := $(patsubst %.c,$(COMPARISON_WASI)/%.wasm,$(COMPARISON_SRCS))

# Build comparison tests for native
.PHONY: comparison-native
comparison-native: $(COMPARISON_NATIVE_BINS)

$(COMPARISON_NATIVE):
	@mkdir -p $@

$(COMPARISON_NATIVE)/%: $(COMPARISON_DIR)/%.c | $(COMPARISON_NATIVE)
	@echo "  CC [native] $<"
	@$(CC) $(CFLAGS) $< -o $@

# Build comparison tests for WASI
.PHONY: comparison-wasi
comparison-wasi: $(COMPARISON_WASI_WASMS)

$(COMPARISON_WASI):
	@mkdir -p $@

$(COMPARISON_WASI)/%.wasm: $(COMPARISON_DIR)/%.c | $(COMPARISON_WASI)
	@echo "  CC [wasi]   $<"
	@$(CC_WASI) $(CFLAGS) $< -o $@

# Build both comparison test versions
.PHONY: comparison-build
comparison-build: comparison-native comparison-wasi

# Run comparison tests
.PHONY: comparison-test
comparison-test: comparison-build
	@echo ""
	@echo "==============================================="
	@echo "WASI Comparison Tests"
	@echo "==============================================="
	@for base in $(basename $(COMPARISON_SRCS)); do \
		echo ""; \
		echo "=== $$base ==="; \
		echo "Native:"; \
		$(COMPARISON_NATIVE)/$$base 2>&1 | tail -3; \
		echo "Wasmtime:"; \
		$(WASMTIME) run --dir=. $(COMPARISON_WASI)/$$base.wasm 2>&1 | tail -3; \
	done

# ============================================================================
# Capstone Integration Tests
# ============================================================================

CAPSTONE_DIR := $(TEST_DIR)/capstone
CAPSTONE_BUILD := $(BUILD_DIR)/capstone

$(CAPSTONE_BUILD):
	@mkdir -p $@

# Build all capstone tests
.PHONY: capstone-build
capstone-build: $(CAPSTONE_BUILD)
	@echo "Building capstone tests (native)..."
	@$(CC) -Wall -Wextra -std=c11 -g -O0 \
		$(CAPSTONE_DIR)/capstone_test.c \
		-o $(CAPSTONE_BUILD)/capstone_native
	@$(CC) -Wall -Wextra -std=c11 -g -O0 \
		$(CAPSTONE_DIR)/capstone_pipeline.c \
		-o $(CAPSTONE_BUILD)/pipeline_native -lm
	@$(CC) -Wall -Wextra -std=c11 -g -O0 \
		$(CAPSTONE_DIR)/capstone_tree.c \
		-o $(CAPSTONE_BUILD)/tree_native
	@echo "Building capstone tests (wasi)..."
	@$(CC_WASI) -Wall -Wextra -O2 \
		$(CAPSTONE_DIR)/capstone_wasm.c \
		-o $(CAPSTONE_BUILD)/capstone.wasm
	@$(CC_WASI) -Wall -Wextra -O2 \
		$(CAPSTONE_DIR)/capstone_pipeline.c \
		-o $(CAPSTONE_BUILD)/pipeline.wasm
	@$(CC_WASI) -Wall -Wextra -O2 \
		$(CAPSTONE_DIR)/capstone_tree.c \
		-o $(CAPSTONE_BUILD)/tree.wasm

# Run original capstone test
.PHONY: capstone-test
capstone-test: capstone-build
	@echo ""
	@echo "==============================================="
	@echo "Capstone Integration Test (Original)"
	@echo "==============================================="
	@echo ""
	@mkdir -p $(CAPSTONE_BUILD)/testenv
	@echo "Running native capstone test..."
	@cd $(CAPSTONE_BUILD)/testenv && ../capstone_native 2>&1 | tee ../native.log
	@echo ""
	@echo "Running Wasmtime capstone test..."
	@cd $(CAPSTONE_BUILD)/testenv && $(WASMTIME) run --dir=. --env=TEST_MODE=wasi ../capstone.wasm 2>&1 | tee ../wasi.log
	@echo ""
	@echo "Comparing outputs..."
	@diff -u $(CAPSTONE_BUILD)/native.log $(CAPSTONE_BUILD)/wasi.log && echo "PASS: Outputs identical" || echo "DIFF: Outputs differ (see above)"

# Run capstone pipeline test
.PHONY: capstone-pipeline
capstone-pipeline: capstone-build
	@echo ""
	@echo "==============================================="
	@echo "Capstone Test 2: Data Processing Pipeline"
	@echo "==============================================="
	@echo ""
	@mkdir -p $(CAPSTONE_BUILD)/testenv
	@echo "Running native pipeline test..."
	@cd $(CAPSTONE_BUILD)/testenv && ../pipeline_native 2>&1 | tee ../pipeline_native.log
	@echo ""
	@echo "Running Wasmtime pipeline test..."
	@cd $(CAPSTONE_BUILD)/testenv && $(WASMTIME) run --dir=. --env=TEST_VAR=wasi_test ../pipeline.wasm 2>&1 | tee ../pipeline_wasi.log
	@echo ""
	@echo "Comparing results (filtering non-deterministic values)..."
	@grep -E "^\s*(PASS|FAIL|Results):" $(CAPSTONE_BUILD)/pipeline_native.log > $(CAPSTONE_BUILD)/pipeline_native_results.txt || true
	@grep -E "^\s*(PASS|FAIL|Results):" $(CAPSTONE_BUILD)/pipeline_wasi.log > $(CAPSTONE_BUILD)/pipeline_wasi_results.txt || true
	@diff -u $(CAPSTONE_BUILD)/pipeline_native_results.txt $(CAPSTONE_BUILD)/pipeline_wasi_results.txt && echo "PASS: Test results match" || echo "DIFF: Test results differ (see above)"

# Run capstone tree test
.PHONY: capstone-tree
capstone-tree: capstone-build
	@echo ""
	@echo "==============================================="
	@echo "Capstone Test 3: Recursive Directory Tree"
	@echo "==============================================="
	@echo ""
	@mkdir -p $(CAPSTONE_BUILD)/testenv
	@echo "Running native tree test..."
	@cd $(CAPSTONE_BUILD)/testenv && ../tree_native 2>&1 | tee ../tree_native.log
	@echo ""
	@echo "Running Wasmtime tree test..."
	@cd $(CAPSTONE_BUILD)/testenv && $(WASMTIME) run --dir=. ../tree.wasm 2>&1 | tee ../tree_wasi.log
	@echo ""
	@echo "Comparing results (filtering non-deterministic values)..."
	@grep -E "^\s*(PASS|FAIL|Results):" $(CAPSTONE_BUILD)/tree_native.log > $(CAPSTONE_BUILD)/tree_native_results.txt || true
	@grep -E "^\s*(PASS|FAIL|Results):" $(CAPSTONE_BUILD)/tree_wasi.log > $(CAPSTONE_BUILD)/tree_wasi_results.txt || true
	@diff -u $(CAPSTONE_BUILD)/tree_native_results.txt $(CAPSTONE_BUILD)/tree_wasi_results.txt && echo "PASS: Test results match" || echo "DIFF: Test results differ (see above)"

# Run all capstone tests
.PHONY: capstone-all
capstone-all: capstone-test capstone-pipeline capstone-tree
	@echo ""
	@echo "==============================================="
	@echo "All Capstone Tests Complete"
	@echo "==============================================="

# ============================================================================
# Unified Test Targets
# ============================================================================

# Run all unit tests
.PHONY: test-all-unit
test-all-unit: test
	@echo "All unit tests complete."

# Run all comparison tests
.PHONY: test-all-comparison
test-all-comparison: comparison-test
	@echo "All comparison tests complete."

# Run all capstone tests
.PHONY: test-all-capstone
test-all-capstone: capstone-all
	@echo "All capstone tests complete."

# Run ALL tests (unit + comparison + capstone)
.PHONY: test-all
test-all: test-all-unit test-all-comparison test-all-capstone
	@echo ""
	@echo "==============================================="
	@echo "ALL TESTS COMPLETE"
	@echo "==============================================="

# Check that generated code compiles
.PHONY: check-bindings
check-bindings: v0.2.0
	@echo "Checking that generated bindings compile..."
	@for dir in $(BINDINGS_DIR)/*/; do \
		name=$$(basename $$dir); \
		echo "  Checking $$name..."; \
		$(CC) -fsyntax-only -c "$$dir"/*.h 2>&1 || exit 1; \
	done
	@echo "All bindings compile successfully."

.DEFAULT_GOAL := all
