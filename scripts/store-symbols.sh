#!/usr/bin/env bash
# Files the debug information of one build in a symbol store, under its build id.
#
#   scripts/store-symbols.sh BINARY [STORE]
#
# STORE defaults to ./symbols. Keep the store for as long as you keep the releases it belongs to; scripts/crash-report.sh
# takes the store as its second argument. Prints the build id and the path of each stored file. Exit status: 0 stored,
# 2 nothing to store (no build id or no debug information) or the copy failed, 64 bad usage, 127 a required tool is missing.
#
# Linux: BINARY is the unstripped program, or its .debug file. The layout, both of which debuggers look for:
#   STORE/<name>/<build-id>/<name>.debug         one directory per build, easy to read and to archive
#   STORE/.build-id/<xx>/<rest>.debug            the layout gdb and lldb search by build id (a link to the file above)
#
# macOS (Phase 21 Day 292): BINARY is the program, or its .dSYM bundle. A program's dSYM is the bundle beside it
# (<program>.dSYM, as the build's engine_dsym makes it) when its UUID matches, else dsymutil makes one (it reads the
# object files of the build, so run it before they are cleaned). The layout, one bundle per UUID (the LC_UUID, as
# `dwarfdump --uuid` prints it):
#   STORE/<name>/<UUID>/<name>.dSYM              what lldb's `target symbols add` and crash-report.sh load
# A universal (several-architecture) dSYM is filed once per architecture, each copy holding only its own (ditto --arch).
set -eu
bin="${1:-}"
store="${2:-symbols}"
if [ -z "$bin" ] || [ ! -e "$bin" ]; then
  echo "usage: store-symbols.sh BINARY [STORE]" >&2
  exit 64
fi
bin="${bin%/}"

if [ "$(uname -s)" = "Darwin" ]; then
  for tool in dwarfdump dsymutil ditto otool; do
    command -v "$tool" >/dev/null 2>&1 || { echo "store-symbols.sh: $tool is missing (the Command Line Tools have it: xcode-select --install)" >&2; exit 127; }
  done
  # The UUIDs of a Mach-O file or bundle, one "UUID ARCH" line per architecture.
  uuids() { dwarfdump --uuid "$1" 2>/dev/null | sed -n 's/^UUID: \([0-9A-F-]*\) (\([^)]*\)).*/\1 \2/p'; }
  work=""
  cleanup() { if [ -n "$work" ]; then rm -rf "$work"; fi; }
  trap cleanup EXIT
  case "$bin" in
    *.dSYM)
      dsym="$bin"
      name="$(basename "$bin" .dSYM)"
      ;;
    *)
      name="$(basename "$bin")"
      want="$(uuids "$bin")"
      if [ -z "$want" ]; then
        echo "store-symbols.sh: $bin has no LC_UUID (not a Mach-O program or library?)" >&2
        exit 2
      fi
      if [ -d "$bin.dSYM" ] && [ "$(uuids "$bin.dSYM")" = "$want" ]; then
        dsym="$bin.dSYM"
      else
        work="$(mktemp -d "${TMPDIR:-/tmp}/store-symbols.XXXXXX")"
        dsym="$work/$name.dSYM"
        dsymutil "$bin" -o "$dsym" >/dev/null 2>&1 || true
      fi
      ;;
  esac
  # Without DWARF there is nothing worth storing (a program built without -g, or stripped before dsymutil ran: dsymutil
  # then writes a bundle with no __debug_info).
  dwarf_dir="$dsym/Contents/Resources/DWARF"
  dwarf=""
  [ -d "$dwarf_dir" ] && dwarf="$(find "$dwarf_dir" -type f | head -n 1)"
  if [ -z "$dwarf" ] || ! otool -l "$dwarf" 2>/dev/null | grep -q 'sectname __debug_info'; then
    echo "store-symbols.sh: $bin has no debug information (build with -g, and give the program from before it was stripped, or its .dSYM)" >&2
    exit 2
  fi
  lines="$(uuids "$dsym")"
  if [ -z "$lines" ]; then
    echo "store-symbols.sh: $dsym has no UUID" >&2
    exit 2
  fi
  count="$(printf '%s\n' "$lines" | wc -l | tr -d ' ')"
  while read -r id arch; do
    dir="$store/$name/$id"
    mkdir -p "$dir"
    # Written beside the old bundle and swapped in when complete, so a failed copy keeps the bundle already stored.
    fresh="$dir/.$name.dSYM.new"
    rm -rf "$fresh"
    if [ "$count" -gt 1 ]; then
      copied=0; ditto --arch "$arch" "$dsym" "$fresh" && copied=1
    else
      copied=0; ditto "$dsym" "$fresh" && copied=1
    fi
    if [ $copied -eq 0 ]; then
      rm -rf "$fresh"
      echo "store-symbols.sh: could not copy $dsym into $dir (the bundle stored before, if any, is kept)" >&2
      exit 2
    fi
    chmod -R u+rwX,go+rX "$fresh"
    rm -rf "$dir/$name.dSYM"
    mv "$fresh" "$dir/$name.dSYM"
    echo "$id"
    echo "$dir/$name.dSYM"
  done <<< "$lines"
  exit 0
fi

if [ ! -f "$bin" ]; then
  echo "usage: store-symbols.sh BINARY [STORE]" >&2
  exit 64
fi
id="$(readelf -n "$bin" 2>/dev/null | sed -n 's/.*Build ID: *//p' | head -n 1)"
if [ -z "$id" ]; then
  echo "store-symbols.sh: $bin has no GNU build id (link with -Wl,--build-id)" >&2
  exit 2
fi
# Without debug sections there is nothing worth storing.
if ! readelf -S "$bin" 2>/dev/null | grep -q '\.debug_info'; then
  echo "store-symbols.sh: $bin has no debug information (build with -g, and give the file from before it was stripped)" >&2
  exit 2
fi
name="$(basename "$bin")"
name="${name%.debug}"
dir="$store/$name/$id"
mkdir -p "$dir" "$store/.build-id/${id:0:2}"
if [ "${bin%.debug}" = "$bin" ]; then
  objcopy --only-keep-debug "$bin" "$dir/$name.debug"
else
  cp "$bin" "$dir/$name.debug"
fi
chmod 644 "$dir/$name.debug"
# A relative link, so the store can be moved, archived or copied to another host without breaking.
ln -sf "../../$name/$id/$name.debug" "$store/.build-id/${id:0:2}/${id:2}.debug"
echo "$id"
echo "$dir/$name.debug"
