# Makefile for WASI Comparison Tests
#
# Builds tests for both native and WASI (WebAssembly) targets
# to compare behavior against Wasmtime's WASI Preview 2 implementation.

WASI_SDK_PATH ?= /opt/wasi-sdk-25.0-x86_64-linux
WASMTIME ?= $(HOME)/.wasmtime/bin/wasmtime

# Compilers
CC_NATIVE := gcc
CC_WASI := $(WASI_SDK_PATH)/bin/wasm32-wasip2-clang

# Flags
CFLAGS_NATIVE := -Wall -Wextra -O2
CFLAGS_WASI := -Wall -Wextra -O2

# Output directories
BUILD_DIR := ../../build/wasm-comparison
NATIVE_DIR := $(BUILD_DIR)/native
WASI_DIR := $(BUILD_DIR)/wasi

# Test sources
TEST_SRCS := test_random.c test_clocks.c test_filesystem.c test_env.c

# Output files
NATIVE_BINS := $(patsubst %.c,$(NATIVE_DIR)/%,$(TEST_SRCS))
WASI_WASMS := $(patsubst %.c,$(WASI_DIR)/%.wasm,$(TEST_SRCS))

.PHONY: all native wasi clean run-native run-wasi compare help

all: native wasi

help:
	@echo "WASI Comparison Test Suite"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build both native and WASI versions"
	@echo "  native      - Build native Linux binaries"
	@echo "  wasi        - Build WebAssembly modules"
	@echo "  run-native  - Run native tests"
	@echo "  run-wasi    - Run tests in Wasmtime"
	@echo "  compare     - Run both and show side-by-side comparison"
	@echo "  clean       - Remove build artifacts"
	@echo ""
	@echo "Environment:"
	@echo "  WASI_SDK_PATH: $(WASI_SDK_PATH)"
	@echo "  WASMTIME:      $(WASMTIME)"

# Create output directories
$(NATIVE_DIR) $(WASI_DIR):
	@mkdir -p $@

# Build native binaries
native: $(NATIVE_DIR) $(NATIVE_BINS)

$(NATIVE_DIR)/%: %.c | $(NATIVE_DIR)
	@echo "  CC [native] $<"
	@$(CC_NATIVE) $(CFLAGS_NATIVE) $< -o $@

# Build WASI modules
wasi: $(WASI_DIR) $(WASI_WASMS)

$(WASI_DIR)/%.wasm: %.c | $(WASI_DIR)
	@echo "  CC [wasi]   $<"
	@$(CC_WASI) $(CFLAGS_WASI) $< -o $@

# Run native tests
run-native: native
	@echo ""
	@echo "==============================================="
	@echo "Running Native Tests"
	@echo "==============================================="
	@for test in $(NATIVE_BINS); do \
		echo ""; \
		echo "--- Running $$(basename $$test) ---"; \
		$$test || true; \
	done

# Run WASI tests in Wasmtime
run-wasi: wasi
	@echo ""
	@echo "==============================================="
	@echo "Running WASI Tests in Wasmtime"
	@echo "==============================================="
	@for wasm in $(WASI_WASMS); do \
		echo ""; \
		echo "--- Running $$(basename $$wasm) ---"; \
		$(WASMTIME) run --dir=. $$wasm || true; \
	done

# Compare results
compare: native wasi
	@echo ""
	@echo "==============================================="
	@echo "WASI Comparison Test Results"
	@echo "==============================================="
	@echo ""
	@echo "Running each test on both platforms..."
	@echo ""
	@for base in $(basename $(TEST_SRCS)); do \
		echo "============================================"; \
		echo "Test: $$base"; \
		echo "============================================"; \
		echo ""; \
		echo "--- Native Linux ---"; \
		$(NATIVE_DIR)/$$base 2>&1 | tee /tmp/native_$$base.out; \
		echo ""; \
		echo "--- Wasmtime WASI Preview 2 ---"; \
		$(WASMTIME) run --dir=. $(WASI_DIR)/$$base.wasm 2>&1 | tee /tmp/wasi_$$base.out; \
		echo ""; \
		if diff -q /tmp/native_$$base.out /tmp/wasi_$$base.out > /dev/null 2>&1; then \
			echo "RESULT: ✓ Outputs IDENTICAL"; \
		else \
			echo "RESULT: ⚠ Outputs DIFFER"; \
			echo "Differences:"; \
			diff /tmp/native_$$base.out /tmp/wasi_$$base.out | head -20 || true; \
		fi; \
		echo ""; \
	done

clean:
	@rm -rf $(BUILD_DIR)
	@echo "Cleaned build artifacts"

.PRECIOUS: $(NATIVE_BINS) $(WASI_WASMS)
