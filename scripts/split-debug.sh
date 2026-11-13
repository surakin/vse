#!/usr/bin/env bash
# Splits the debug information out of a program: writes its debug file next to it and strips the program.
#
#   scripts/split-debug.sh BINARY
#
# Linux: <binary>.debug (binutils objcopy and strip), and a link from the program to the debug file. The program keeps
# its GNU build id, so a debugger or scripts/crash-report.sh can find the debug file again from a symbol store
# (scripts/store-symbols.sh). The CMake option ENGINE_SPLIT_DEBUG_INFO runs this after linking.
# macOS (Phase 21 Day 292): <binary>.dSYM (dsymutil, which reads the build's object files), then `strip -S`, which
# removes the debug map and keeps the symbol table, so a crash report still names functions; the LC_UUID ties the
# program to its dSYM. The build makes the dSYM itself (engine_dsym); this is for a program built without it.
set -eu
bin="${1:-}"
if [ -z "$bin" ] || [ ! -f "$bin" ]; then
  echo "usage: split-debug.sh BINARY" >&2
  exit 64
fi
if [ "$(uname -s)" = "Darwin" ]; then
  command -v dsymutil >/dev/null 2>&1 && command -v strip >/dev/null 2>&1 && command -v nm >/dev/null 2>&1 || { echo "split-debug.sh: dsymutil, strip and nm are required (xcode-select --install)" >&2; exit 127; }
  # The debug map (N_OSO entries naming the object files) is what dsymutil reads; without it the program is already
  # split or was built without -g, and an existing .dSYM is left alone rather than overwritten with an empty one.
  if ! nm -ap "$bin" 2>/dev/null | grep -q ' OSO '; then
    echo "split-debug.sh: $bin has no debug information (already split, or built without -g); nothing done" >&2
    exit 0
  fi
  dsymutil "$bin" -o "$bin.dSYM"
  strip -S "$bin"
  exit 0
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
