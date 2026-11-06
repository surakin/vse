#!/usr/bin/env bash
# Splits the debug information out of a program: writes <binary>.debug next to it, strips the program, and adds a
# link from the program to the debug file.
#
#   scripts/split-debug.sh BINARY
#
# Linux only (a shell script; binutils objcopy and strip). The program keeps its GNU build id, so a debugger or
# scripts/crash-report.sh can find the debug file again from a symbol store (scripts/store-symbols.sh). The CMake option
# ENGINE_SPLIT_DEBUG_INFO runs this after linking.
set -eu
bin="${1:-}"
if [ -z "$bin" ] || [ ! -f "$bin" ]; then
  echo "usage: split-debug.sh BINARY" >&2
  exit 64
fi
command -v objcopy >/dev/null 2>&1 && command -v strip >/dev/null 2>&1 || { echo "split-debug.sh: objcopy and strip (binutils) are required" >&2; exit 127; }
# Already stripped (a second run, or a build without -g): leave the existing .debug file alone rather than overwrite it with an empty one.
if ! readelf -S "$bin" 2>/dev/null | grep -q '\.debug_info'; then
  echo "split-debug.sh: $bin has no debug information (already split, or built without -g); nothing done" >&2
  exit 0
fi
objcopy --only-keep-debug "$bin" "$bin.debug"
strip --strip-all "$bin"
objcopy --add-gnu-debuglink="$bin.debug" "$bin"
