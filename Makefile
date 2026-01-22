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
#   make setup        - Install WASI SDK and Wasmtime
#   make build        - Build everything (bindings + tests)
#   make test         - Run all tests with unified summary
#   make test-safe    - Run tests in safe mode (non-destructive only)
#   make clean        - Remove generated files
#   make help         - Show this help
#
# Note: WASI v0.2.0 uses a different directory structure (preview2/) than
# later versions (proposals/*/wit/). This Makefile handles both.

SHELL := /bin/bash

# Configuration
BUILD_DIR := build
BINDINGS_DIR := $(BUILD_DIR)/c-bindings
WIT_CACHE_DIR := $(BUILD_DIR)/wit-cache

# Distribution configuration
DIST_DIR := dist
WASI_DIST_VERSION := 0.2.0
DIST_NAME := wasi-c-runtime-$(WASI_DIST_VERSION)
DIST_ROOT := $(DIST_DIR)/$(DIST_NAME)

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
	@echo "Main Targets:"
	@echo "  setup             - Install WASI SDK and Wasmtime (one-time setup)"
	@echo "  build             - Build everything (bindings + all test binaries)"
	@echo "  test              - Run all tests with unified pass/fail summary"
	@echo "  test-safe         - Run tests in safe mode (non-destructive only)"
	@echo "  clean             - Remove all generated files"
	@echo "  help              - Show this help"
	@echo ""
	@echo "Version-specific Targets:"
	@echo "  v0.2.0            - Generate bindings from WASI v0.2.0"
	@echo "  v0.2.X            - Generate bindings from any version (0.2.1-0.2.9)"
	@echo ""
	@echo "Distribution Targets:"
	@echo "  dist              - Create distribution package for MLIR/LLVM integration"
	@echo "  dist-tarball      - Create dist + tarball archive"
	@echo "  dist-clean        - Remove distribution directory"
	@echo ""
	@echo "Other Targets:"
	@echo "  deps              - Fetch WIT dependencies only"
	@echo "  validate          - Validate all WIT files"
	@echo "  proposal-X        - Generate bindings for a single proposal"
	@echo "  check-bindings    - Verify generated bindings compile"
	@echo ""
	@echo "Output:"
	@echo "  $(BINDINGS_DIR)/       - Generated C bindings"
	@echo "  $(DIST_DIR)/           - Distribution package"
	@echo ""
	@echo "Example:"
	@echo "  make setup        # One-time setup"
	@echo "  make build        # Build everything"
	@echo "  make test         # Run all tests"

# List generated files
.PHONY: list
list:
	@if [ -d "$(BINDINGS_DIR)" ]; then \
		echo "Generated files in $(BINDINGS_DIR):"; \
		find $(BINDINGS_DIR) -type f | sort; \
	else \
		echo "No bindings generated yet. Run 'make build' first."; \
	fi

# ============================================================================
# C Implementation Build
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
UNAME_M := $(shell uname -m)

# WASI SDK version for automatic download
WASI_SDK_VERSION := 25
WASI_SDK_FULL_VERSION := 25.0

# Platform detection (OS)
ifeq ($(UNAME_S),Darwin)
    PLATFORM_SRC := $(PLATFORM_DIR)/darwin.c
    WASI_SDK_OS  := macos
else ifeq ($(UNAME_S),Linux)
    PLATFORM_SRC := $(PLATFORM_DIR)/linux.c
    WASI_SDK_OS  := linux
else
    $(error Unsupported OS: $(UNAME_S))
endif

# Architecture detection (supports both macOS arm64 and Linux aarch64)
ifeq ($(UNAME_M),arm64)
    WASI_SDK_ARCH := arm64
else ifeq ($(UNAME_M),aarch64)
    WASI_SDK_ARCH := arm64
else ifeq ($(UNAME_M),x86_64)
    WASI_SDK_ARCH := x86_64
else
    $(warning Unknown arch '$(UNAME_M)'; defaulting to x86_64)
    WASI_SDK_ARCH := x86_64
endif

# Try to find installed wasi-sdk (check local first, then system)
WASI_SDK_PATH_DEFAULT := $(firstword \
    $(wildcard $(CURDIR)/tools/wasi-sdk) \
    $(wildcard $(HOME)/.local/wasi-sdk) \
    $(wildcard /opt/wasi-sdk) \
    $(wildcard /opt/wasi-sdk-*-$(WASI_SDK_ARCH)-$(WASI_SDK_OS)) \
    $(CURDIR)/tools/wasi-sdk)

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

# Build unit test binary
.PHONY: build-unit-tests
build-unit-tests: v0.2.0 $(OBJ_DIR) $(TEST_OBJS) $(WASI_OBJS) $(PLATFORM_OBJS)
	@echo "Linking test binary..."
	@$(CC) $(CFLAGS_DEBUG) -o $(TEST_BIN) $(TEST_OBJS) $(WASI_OBJS) $(PLATFORM_OBJS)

# ============================================================================
# Setup (One-time installation)
# ============================================================================

WASI_SDK_TARBALL := wasi-sdk-$(WASI_SDK_FULL_VERSION)-$(WASI_SDK_ARCH)-$(WASI_SDK_OS).tar.gz
WASI_SDK_URL := https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$(WASI_SDK_VERSION)/$(WASI_SDK_TARBALL)
TOOLS_DIR := $(CURDIR)/tools

# Unified setup target - installs all required tools
.PHONY: setup
setup:
	@echo "WASI SDK $(WASI_SDK_FULL_VERSION) for $(WASI_SDK_ARCH)-$(WASI_SDK_OS)..."
	@mkdir -p $(TOOLS_DIR)
	@if [ -d "$(TOOLS_DIR)/wasi-sdk" ]; then \
		echo " Installed at $(TOOLS_DIR)/wasi-sdk"; \
		$(TOOLS_DIR)/wasi-sdk/bin/clang --version | head -1 | sed 's/^/ /'; \
	else \
		echo "  Downloading $(WASI_SDK_TARBALL)..."; \
		curl -L -o "$(TOOLS_DIR)/$(WASI_SDK_TARBALL)" "$(WASI_SDK_URL)"; \
		echo "  Extracting..."; \
		tar xzf "$(TOOLS_DIR)/$(WASI_SDK_TARBALL)" -C "$(TOOLS_DIR)"; \
		mv "$(TOOLS_DIR)/wasi-sdk-$(WASI_SDK_FULL_VERSION)-$(WASI_SDK_ARCH)-$(WASI_SDK_OS)" "$(TOOLS_DIR)/wasi-sdk"; \
		rm "$(TOOLS_DIR)/$(WASI_SDK_TARBALL)"; \
		echo "  Installed at $(TOOLS_DIR)/wasi-sdk"; \
	fi
	@echo "Wasmtime..."
	@if command -v wasmtime >/dev/null 2>&1; then \
		echo " Installed: $$(wasmtime --version)" ; \
	elif [ -x "$(HOME)/.wasmtime/bin/wasmtime" ]; then \
		echo " Installed at $(HOME)/.wasmtime/bin/wasmtime:"; \
		$(HOME)/.wasmtime/bin/wasmtime --version | sed 's/^/ /'; \
	else \
		echo "  Installing..."; \
		curl https://wasmtime.dev/install.sh -sSf | bash; \
		echo "  Installed."; \
	fi

# ============================================================================
# WASI SDK and Wasmtime Configuration
# ============================================================================

WASI_SDK_PATH ?= $(WASI_SDK_PATH_DEFAULT)
# Find wasmtime: check PATH first, then common install locations
WASMTIME_DEFAULT := $(or \
    $(shell which wasmtime 2>/dev/null),\
    $(wildcard $(HOME)/.wasmtime/bin/wasmtime),\
    $(wildcard /opt/homebrew/bin/wasmtime),\
    wasmtime)
WASMTIME ?= $(WASMTIME_DEFAULT)
CC_WASI := $(WASI_SDK_PATH)/bin/wasm32-wasip2-clang

# ============================================================================
# Comparison Tests
# ============================================================================

COMPARISON_DIR := $(TEST_DIR)/wasm-comparison
COMPARISON_BUILD := $(BUILD_DIR)/wasm-comparison
COMPARISON_NATIVE := $(COMPARISON_BUILD)/native
COMPARISON_WASI := $(COMPARISON_BUILD)/wasi

COMPARISON_SRCS := test_random.c test_clocks.c test_filesystem.c test_env.c test_details.c
COMPARISON_NATIVE_BINS := $(patsubst %.c,$(COMPARISON_NATIVE)/%,$(COMPARISON_SRCS))
COMPARISON_WASI_WASMS := $(patsubst %.c,$(COMPARISON_WASI)/%.wasm,$(COMPARISON_SRCS))

$(COMPARISON_NATIVE):
	@mkdir -p $@

$(COMPARISON_NATIVE)/%: $(COMPARISON_DIR)/%.c | $(COMPARISON_NATIVE)
	@echo "  CC [native] $<"
	@$(CC) $(CFLAGS) $< -o $@

$(COMPARISON_WASI):
	@mkdir -p $@

$(COMPARISON_WASI)/%.wasm: $(COMPARISON_DIR)/%.c | $(COMPARISON_WASI)
	@echo "  CC [wasi]   $<"
	@$(CC_WASI) $(CFLAGS) $< -o $@

.PHONY: build-comparison
build-comparison: $(COMPARISON_NATIVE_BINS) $(COMPARISON_WASI_WASMS)

# ============================================================================
# Capstone Tests
# ============================================================================

CAPSTONE_DIR := $(TEST_DIR)/capstone
CAPSTONE_BUILD := $(BUILD_DIR)/capstone

$(CAPSTONE_BUILD):
	@mkdir -p $@

# Capstone compiler flags
# IMPORTANT: -O0 is required for WASI builds because wasm-opt (Binaryen) does
# not support WebAssembly components yet. Higher optimization levels trigger
# wasm-opt which fails on component binaries.
CAPSTONE_CFLAGS_COMMON := -Wall -Wextra -O0
CAPSTONE_CFLAGS_NATIVE := $(CAPSTONE_CFLAGS_COMMON) -std=c11 -g
CAPSTONE_CFLAGS_WASI   := $(CAPSTONE_CFLAGS_COMMON)

# Build capstone tests (native + WASI)
.PHONY: build-capstone
build-capstone: $(CAPSTONE_BUILD)
	@echo "Building capstone tests..."
	@$(CC) $(CAPSTONE_CFLAGS_NATIVE) $(CAPSTONE_DIR)/capstone_test.c -o $(CAPSTONE_BUILD)/capstone_native
	@$(CC) $(CAPSTONE_CFLAGS_NATIVE) $(CAPSTONE_DIR)/capstone_pipeline.c -o $(CAPSTONE_BUILD)/pipeline_native -lm
	@$(CC) $(CAPSTONE_CFLAGS_NATIVE) $(CAPSTONE_DIR)/capstone_tree.c -o $(CAPSTONE_BUILD)/tree_native
	@$(CC_WASI) $(CAPSTONE_CFLAGS_WASI) $(CAPSTONE_DIR)/capstone_wasm.c -o $(CAPSTONE_BUILD)/capstone.wasm
	@$(CC_WASI) $(CAPSTONE_CFLAGS_WASI) $(CAPSTONE_DIR)/capstone_pipeline.c -o $(CAPSTONE_BUILD)/pipeline.wasm
	@$(CC_WASI) $(CAPSTONE_CFLAGS_WASI) $(CAPSTONE_DIR)/capstone_tree.c -o $(CAPSTONE_BUILD)/tree.wasm

# ============================================================================
# Unified Build Target
# ============================================================================

.PHONY: build
build: v0.2.0 build-unit-tests build-comparison build-capstone
	@echo ""
	@echo "Build complete."
	@echo "  Bindings:    $(BINDINGS_DIR)/"
	@echo "  Unit tests:  $(TEST_BIN)"
	@echo "  Comparison:  $(COMPARISON_BUILD)/"
	@echo "  Capstone:    $(CAPSTONE_BUILD)/"

# ============================================================================
# Unified Test Target
# ============================================================================

# Test results file for aggregation
TEST_RESULTS := $(CURDIR)/$(BUILD_DIR)/test_results.txt
CAPSTONE_TESTENV := $(CURDIR)/$(CAPSTONE_BUILD)/testenv

.PHONY: test
test: build
	@rm -f $(TEST_RESULTS)
	@mkdir -p $(CAPSTONE_TESTENV)
	@total_pass=0; total_fail=0; \
	\
	echo ""; \
	echo "Running unit tests..."; \
	$(CURDIR)/$(TEST_BIN) 2>&1 | tee $(CURDIR)/$(BUILD_DIR)/unit_test.log; \
	unit_pass=$$(grep ': PASS' $(CURDIR)/$(BUILD_DIR)/unit_test.log 2>/dev/null | wc -l | tr -d ' '); \
	unit_fail=$$(grep ': FAIL' $(CURDIR)/$(BUILD_DIR)/unit_test.log 2>/dev/null | wc -l | tr -d ' '); \
	total_pass=$$((total_pass + unit_pass)); \
	total_fail=$$((total_fail + unit_fail)); \
	echo "unit: $$unit_pass passed, $$unit_fail failed" >> $(TEST_RESULTS); \
	\
	echo "Running comparison tests..."; \
	comp_pass=0; comp_fail=0; \
	for base in $(basename $(COMPARISON_SRCS)); do \
		$(CURDIR)/$(COMPARISON_NATIVE)/$$base > $(CURDIR)/$(BUILD_DIR)/comp_native_$$base.log 2>&1; \
		native_fail=$$(grep -oE '[0-9]+ failed' $(CURDIR)/$(BUILD_DIR)/comp_native_$$base.log | grep -oE '[0-9]+' || echo 0); \
		if [ "$$native_fail" = "0" ]; then \
			echo "  $${base}_native: PASS"; \
		else \
			echo "  $${base}_native: FAIL"; \
		fi; \
		$(WASMTIME) run --dir=. --env=WASI_TEST_VAR=1 $(CURDIR)/$(COMPARISON_WASI)/$$base.wasm > $(CURDIR)/$(BUILD_DIR)/comp_wasi_$$base.log 2>&1; \
		wasi_fail=$$(grep -oE '[0-9]+ failed' $(CURDIR)/$(BUILD_DIR)/comp_wasi_$$base.log | grep -oE '[0-9]+' || echo 0); \
		if [ "$$wasi_fail" = "0" ]; then \
			echo "  $${base}_wasi: PASS"; \
		else \
			echo "  $${base}_wasi: FAIL"; \
		fi; \
		if [ "$$native_fail" = "0" ] && [ "$$wasi_fail" = "0" ]; then \
			comp_pass=$$((comp_pass + 2)); \
		elif [ "$$native_fail" = "0" ] || [ "$$wasi_fail" = "0" ]; then \
			comp_pass=$$((comp_pass + 1)); \
			comp_fail=$$((comp_fail + 1)); \
		else \
			comp_fail=$$((comp_fail + 2)); \
		fi; \
	done; \
	total_pass=$$((total_pass + comp_pass)); \
	total_fail=$$((total_fail + comp_fail)); \
	echo "comparison: $$comp_pass passed, $$comp_fail failed" >> $(TEST_RESULTS); \
	\
	echo "Running capstone tests..."; \
	cap_pass=0; cap_fail=0; \
	\
	run_and_count() { \
		name="$$1"; shift; \
		log="$$1"; shift; \
		( cd "$(CAPSTONE_TESTENV)" && "$$@" ) > "$$log" 2>&1; \
		p=$$(grep -oE '[0-9]+ passed' "$$log" | grep -oE '[0-9]+' || echo 0); \
		f=$$(grep -oE '[0-9]+ failed' "$$log" | grep -oE '[0-9]+' || echo 0); \
		if [ "$$f" = "0" ]; then echo "  $$name: PASS"; else echo "  $$name: FAIL"; fi; \
		cap_pass=$$((cap_pass + p)); \
		cap_fail=$$((cap_fail + f)); \
	}; \
	\
	BDIR="$(CURDIR)/$(CAPSTONE_BUILD)"; \
	run_and_count capstone_native "$$BDIR/capstone_native.log" "$$BDIR/capstone_native"; \
	run_and_count capstone_wasi   "$$BDIR/capstone_wasi.log"   "$(WASMTIME)" run --dir=. --env=TEST_MODE=wasi "$$BDIR/capstone.wasm"; \
	\
	run_and_count pipeline_native "$$BDIR/pipeline_native.log" "$$BDIR/pipeline_native"; \
	run_and_count pipeline_wasi   "$$BDIR/pipeline_wasi.log"   "$(WASMTIME)" run --dir=. --env=TEST_VAR=wasi_test "$$BDIR/pipeline.wasm"; \
	\
	run_and_count tree_native     "$$BDIR/tree_native.log"     "$$BDIR/tree_native"; \
	run_and_count tree_wasi       "$$BDIR/tree_wasi.log"       "$(WASMTIME)" run --dir=. "$$BDIR/tree.wasm"; \
	\
	total_pass=$$((total_pass + cap_pass)); \
	total_fail=$$((total_fail + cap_fail)); \
	echo "capstone: $$cap_pass passed, $$cap_fail failed" >> $(TEST_RESULTS); \
	\
	echo ""; \
	echo "Test Summary:"; \
	while IFS= read -r line; do echo "  $$line"; done < $(TEST_RESULTS); \
	echo ""; \
	if [ $$total_fail -eq 0 ]; then \
		echo "PASSED: $$total_pass tests passed, 0 failed"; \
	else \
		echo "FAILED: $$total_pass passed, $$total_fail failed"; \
		exit 1; \
	fi

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
	@echo ""
	@echo "Running tests in SAFE MODE (non-destructive operations only)..."
	@echo "Note: Tests requiring destructive filesystem operations will fail."
	@echo ""
	@$(TEST_BIN_SAFE) || true

# ============================================================================
# Check bindings compile
# ============================================================================

.PHONY: check-bindings
check-bindings: v0.2.0
	@echo "Checking that generated bindings compile..."
	@for dir in $(BINDINGS_DIR)/*/; do \
		name=$$(basename $$dir); \
		echo "  Checking $$name..."; \
		$(CC) -fsyntax-only -c "$$dir"/*.h 2>&1 || exit 1; \
	done
	@echo "All bindings compile successfully."

# ============================================================================
# Distribution Package
# ============================================================================

.PHONY: dist
dist: v0.2.0
	@./scripts/create-dist.sh "$(DIST_ROOT)" "$(WIT_CACHE_DIR)" "$(BINDINGS_DIR)" "$(WASI_DIST_VERSION)"

.PHONY: dist-tarball
dist-tarball: dist
	@echo "Creating tarball..."
	@cd $(DIST_DIR) && tar -czf $(DIST_NAME).tar.gz $(DIST_NAME)
	@echo "Created: $(DIST_DIR)/$(DIST_NAME).tar.gz"

.PHONY: dist-clean
dist-clean:
	@rm -rf $(DIST_DIR)
	@echo "Distribution cleaned."

.DEFAULT_GOAL := all
