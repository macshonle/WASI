# HTTP Integration Tests Makefile
#
# This Makefile builds and runs HTTP integration tests against a mock server.
#
# Usage:
#   make build    - Build test binaries
#   make test     - Start mock server and run tests
#   make clean    - Remove build artifacts

# Configuration
BUILD_DIR := ../../build/http-integration
MOCK_SERVER_PORT := 18080
MOCK_SERVER_PID := $(BUILD_DIR)/mock_server.pid

# Compiler settings
CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -g -O0
CFLAGS += -I../../build/c-bindings

# WASI SDK settings
WASI_SDK_PATH ?= $(firstword \
    $(wildcard ../../tools/wasi-sdk) \
    $(wildcard $(HOME)/.local/wasi-sdk) \
    $(wildcard /opt/wasi-sdk))
CC_WASI := $(WASI_SDK_PATH)/bin/wasm32-wasip2-clang

# Wasmtime
WASMTIME := $(or \
    $(shell which wasmtime 2>/dev/null),\
    $(wildcard $(HOME)/.wasmtime/bin/wasmtime))

# Source files
TEST_SRCS := test_client.c

# Build directories
$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/native:
	@mkdir -p $@

$(BUILD_DIR)/wasi:
	@mkdir -p $@

# Check for pnpm
.PHONY: check-pnpm
check-pnpm:
	@command -v pnpm >/dev/null 2>&1 || { echo "pnpm not found. Install with: npm install -g pnpm"; exit 1; }

# Install Node.js dependencies
.PHONY: install-deps
install-deps: check-pnpm
	@pnpm install

# Start mock server
.PHONY: start-server
start-server: $(BUILD_DIR)
	@echo "Starting mock server on port $(MOCK_SERVER_PORT)..."
	@node mock_server.js $(MOCK_SERVER_PORT) > $(BUILD_DIR)/mock_server.log 2>&1 & echo $$! > $(MOCK_SERVER_PID)
	@sleep 2
	@if ! kill -0 $$(cat $(MOCK_SERVER_PID)) 2>/dev/null; then \
		echo "Failed to start mock server. Check $(BUILD_DIR)/mock_server.log"; \
		exit 1; \
	fi
	@echo "Mock server started with PID $$(cat $(MOCK_SERVER_PID))"

# Stop mock server
.PHONY: stop-server
stop-server:
	@if [ -f $(MOCK_SERVER_PID) ]; then \
		echo "Stopping mock server..."; \
		kill $$(cat $(MOCK_SERVER_PID)) 2>/dev/null || true; \
		rm -f $(MOCK_SERVER_PID); \
	fi

# Build native test binaries
.PHONY: build-native
build-native: $(BUILD_DIR)/native
	@echo "Building native test binaries..."
	@for src in $(TEST_SRCS); do \
		name=$$(basename $$src .c); \
		echo "  CC [native] $$src"; \
		$(CC) $(CFLAGS) $$src -o $(BUILD_DIR)/native/$$name 2>&1 || true; \
	done

# Build WASI test binaries
.PHONY: build-wasi
build-wasi: $(BUILD_DIR)/wasi
	@if [ -x "$(CC_WASI)" ]; then \
		echo "Building WASI test binaries..."; \
		for src in $(TEST_SRCS); do \
			name=$$(basename $$src .c); \
			echo "  CC [wasi] $$src"; \
			$(CC_WASI) $(CFLAGS) $$src -o $(BUILD_DIR)/wasi/$$name.wasm 2>&1 || true; \
		done; \
	else \
		echo "WASI SDK not found, skipping WASI builds"; \
	fi

# Build all
.PHONY: build
build: build-native build-wasi

# Run tests
.PHONY: test
test: install-deps start-server build
	@echo ""
	@echo "Running HTTP integration tests..."
	@echo ""
	@test_pass=0; test_fail=0; \
	for src in $(TEST_SRCS); do \
		name=$$(basename $$src .c); \
		echo "Testing $$name..."; \
		if [ -x $(BUILD_DIR)/native/$$name ]; then \
			echo "  Running native..."; \
			$(BUILD_DIR)/native/$$name $(MOCK_SERVER_PORT) > $(BUILD_DIR)/$${name}_native.log 2>&1; \
			if [ $$? -eq 0 ]; then \
				echo "    PASS"; \
				test_pass=$$((test_pass + 1)); \
			else \
				echo "    FAIL (see $(BUILD_DIR)/$${name}_native.log)"; \
				test_fail=$$((test_fail + 1)); \
			fi; \
		fi; \
		if [ -f $(BUILD_DIR)/wasi/$$name.wasm ] && [ -x "$(WASMTIME)" ]; then \
			echo "  Running WASI..."; \
			$(WASMTIME) run --dir=. $(BUILD_DIR)/wasi/$$name.wasm $(MOCK_SERVER_PORT) > $(BUILD_DIR)/$${name}_wasi.log 2>&1; \
			if [ $$? -eq 0 ]; then \
				echo "    PASS"; \
				test_pass=$$((test_pass + 1)); \
			else \
				echo "    FAIL (see $(BUILD_DIR)/$${name}_wasi.log)"; \
				test_fail=$$((test_fail + 1)); \
			fi; \
		fi; \
	done; \
	$(MAKE) stop-server; \
	echo ""; \
	echo "HTTP Integration Tests: $$test_pass passed, $$test_fail failed"; \
	exit $$test_fail

# Clean
.PHONY: clean
clean: stop-server
	rm -rf $(BUILD_DIR)

# Help
.PHONY: help
help:
	@echo "HTTP Integration Tests"
	@echo ""
	@echo "Targets:"
	@echo "  build       - Build test binaries (native + WASI)"
	@echo "  test        - Run all integration tests"
	@echo "  clean       - Remove build artifacts"
	@echo ""
	@echo "Mock Server:"
	@echo "  start-server - Start the mock server"
	@echo "  stop-server  - Stop the mock server"
	@echo ""
	@echo "Configuration:"
	@echo "  MOCK_SERVER_PORT=$(MOCK_SERVER_PORT)"

.DEFAULT_GOAL := test
