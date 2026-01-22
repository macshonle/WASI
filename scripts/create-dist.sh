#!/bin/bash
# create-dist.sh - Create WASI C Runtime distribution package
# Compatible with GNU bash 3.2.57+ (macOS default)
#
# Usage: create-dist.sh DIST_ROOT WIT_CACHE_DIR BINDINGS_DIR [VERSION]
#
# Creates a distribution package containing:
#   - WIT interface definitions
#   - C implementation sources
#   - Generated binding headers
#   - CMake integration files

set -e

# Parameters (passed from Makefile)
DIST_ROOT="${1:?Usage: create-dist.sh DIST_ROOT WIT_CACHE BINDINGS_DIR [VERSION]}"
WIT_CACHE_DIR="${2:?Missing WIT_CACHE_DIR}"
BINDINGS_DIR="${3:?Missing BINDINGS_DIR}"
WASI_VERSION="${4:-0.2.0}"

# Proposals to include
PROPOSALS="io random clocks filesystem sockets cli http"
IMPL_MODULES="common io random clocks filesystem sockets cli http"

echo "Creating distribution: $DIST_ROOT"

# Clean and create root
rm -rf "$DIST_ROOT"
mkdir -p "$DIST_ROOT"

# --- VERSION metadata ---
cat > "$DIST_ROOT/VERSION" << EOF
$WASI_VERSION
WASI_VERSION=$WASI_VERSION
BUILD_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')
EOF

# --- License ---
cp LICENSE.md "$DIST_ROOT/LICENSE"

# --- WIT files ---
echo "  Copying WIT files..."
mkdir -p "$DIST_ROOT/wit"
for proposal in $PROPOSALS; do
    mkdir -p "$DIST_ROOT/wit/$proposal"
    cp "$WIT_CACHE_DIR/preview2/$proposal"/*.wit "$DIST_ROOT/wit/$proposal/" 2>/dev/null || true
done

# --- Include headers ---
echo "  Copying headers..."
mkdir -p "$DIST_ROOT/include/wasi"
mkdir -p "$DIST_ROOT/include/bindings"
cp src/wasi/common.h "$DIST_ROOT/include/wasi/"
cp src/wasi/platform/platform.h "$DIST_ROOT/include/wasi/"
cp include/wasi_compat.h "$DIST_ROOT/include/"

for proposal in $PROPOSALS; do
    mkdir -p "$DIST_ROOT/include/bindings/$proposal"
    cp "$BINDINGS_DIR/$proposal"/*.h "$DIST_ROOT/include/bindings/$proposal/" 2>/dev/null || true
done

# --- Source files ---
# Preserve the original directory structure (src/wasi/) so relative includes work:
#   src/wasi/io.c includes "../../build/c-bindings/io/imports.h"
#   This resolves to: build/c-bindings/io/imports.h (relative to dist root)
echo "  Copying source files..."
mkdir -p "$DIST_ROOT/src/wasi/platform"
for module in $IMPL_MODULES; do
    cp "src/wasi/$module.c" "$DIST_ROOT/src/wasi/"
done
cp src/wasi/platform/darwin.c "$DIST_ROOT/src/wasi/platform/"
cp src/wasi/platform/linux.c "$DIST_ROOT/src/wasi/platform/"

# Copy headers to src/wasi/ for relative includes used by source files
cp src/wasi/common.h "$DIST_ROOT/src/wasi/"
cp src/wasi/platform/platform.h "$DIST_ROOT/src/wasi/platform/"

# Create build/c-bindings structure for source file relative includes
# Source files use: #include "../../build/c-bindings/io/imports.h"
# From src/wasi/io.c, going ../../ reaches the dist root
mkdir -p "$DIST_ROOT/build/c-bindings"
for proposal in $PROPOSALS; do
    mkdir -p "$DIST_ROOT/build/c-bindings/$proposal"
    cp "$BINDINGS_DIR/$proposal"/*.h "$DIST_ROOT/build/c-bindings/$proposal/" 2>/dev/null || true
done

# --- CMake integration ---
echo "  Generating CMake files..."
mkdir -p "$DIST_ROOT/cmake"

cat > "$DIST_ROOT/cmake/WasiCRuntimeConfig.cmake" << 'CMAKEOF'
# WasiCRuntimeConfig.cmake - Find module for WASI C Runtime
#
# Provides:
#   WasiCRuntime_FOUND          - True if found
#   WasiCRuntime_VERSION        - Version string
#   WasiCRuntime_INCLUDE_DIRS   - Include directories
#   WasiCRuntime_SOURCES        - Source files
#   WasiCRuntime_WIT_DIRS       - WIT file directories
#
# Targets:
#   WasiCRuntime::runtime       - Interface library

get_filename_component(_WASI_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_WASI_PREFIX "${_WASI_CMAKE_DIR}/.." ABSOLUTE)

# Read version
file(STRINGS "${_WASI_PREFIX}/VERSION" _WASI_VERSION_LINE LIMIT_COUNT 1)
set(WasiCRuntime_VERSION "${_WASI_VERSION_LINE}")

# Include directories
set(WasiCRuntime_INCLUDE_DIRS
    "${_WASI_PREFIX}/include"
    "${_WASI_PREFIX}/include/bindings"
)

# WIT directories
set(WasiCRuntime_WIT_DIRS "${_WASI_PREFIX}/wit")

# Source files (platform-independent)
file(GLOB _WASI_CORE_SOURCES "${_WASI_PREFIX}/src/wasi/*.c")
set(WasiCRuntime_SOURCES ${_WASI_CORE_SOURCES})

# Platform-specific source
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND WasiCRuntime_SOURCES "${_WASI_PREFIX}/src/wasi/platform/darwin.c")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND WasiCRuntime_SOURCES "${_WASI_PREFIX}/src/wasi/platform/linux.c")
else()
    message(WARNING "WASI C Runtime: Unsupported platform ${CMAKE_SYSTEM_NAME}")
endif()

# Interface target
if(NOT TARGET WasiCRuntime::runtime)
    add_library(WasiCRuntime::runtime INTERFACE IMPORTED)
    set_target_properties(WasiCRuntime::runtime PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${WasiCRuntime_INCLUDE_DIRS}"
    )
endif()

set(WasiCRuntime_FOUND TRUE)
CMAKEOF

cat > "$DIST_ROOT/cmake/WasiCRuntimeConfigVersion.cmake" << CMAKEVOF
set(PACKAGE_VERSION "$WASI_VERSION")
if("\${PACKAGE_FIND_VERSION}" VERSION_EQUAL "$WASI_VERSION")
    set(PACKAGE_VERSION_EXACT TRUE)
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
elseif("\${PACKAGE_FIND_VERSION}" VERSION_LESS "$WASI_VERSION")
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
else()
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
endif()
CMAKEVOF

# --- README ---
echo "  Generating README..."
cat > "$DIST_ROOT/README.md" << 'READMEOF'
# WASI C Runtime Distribution

C implementation of WASI (WebAssembly System Interface) Preview 2 for native host environments.

## Quick Start (CMake)

```cmake
list(APPEND CMAKE_PREFIX_PATH "/path/to/wasi-c-runtime-0.2.0")
find_package(WasiCRuntime REQUIRED)

add_library(wasi_runtime STATIC ${WasiCRuntime_SOURCES})
target_include_directories(wasi_runtime PUBLIC ${WasiCRuntime_INCLUDE_DIRS})
target_compile_options(wasi_runtime PRIVATE -std=c11 -Wall)
```

## Manual Build

```bash
cd wasi-c-runtime-0.2.0
gcc -c -std=c11 src/wasi/*.c
gcc -c -std=c11 src/wasi/platform/$(uname -s | tr A-Z a-z).c
ar rcs libwasi_runtime.a *.o
```

## Contents

- `wit/` - WIT interface definitions for MLIR/LLVM processing
- `include/` - Public headers (wasi/, bindings/)
- `src/wasi/` - Implementation sources (preserves original path structure)
- `build/c-bindings/` - Generated binding headers
- `cmake/` - CMake find module

## Module Dependencies

```
io, random (no deps)
    |
    v
clocks (depends on io)
    |
    v
filesystem, sockets (depend on io, clocks)
    |
    v
cli (depends on io)
    |
    v
http (depends on io, clocks, cli)
```

## Platform Support

- macOS (Darwin): arm64, x86_64
- Linux: x86_64, aarch64
READMEOF

echo "Distribution created: $DIST_ROOT"
file_count=$(find "$DIST_ROOT" -type f | wc -l | tr -d ' ')
echo "  $file_count files"
