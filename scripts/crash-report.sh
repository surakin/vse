#!/usr/bin/env bash
# Turns a crash dump into a readable trace.
#
#   scripts/crash-report.sh DUMP-OR-ENVELOPE [EXECUTABLE-OR-SYMBOL-STORE]
#
# Linux shell script (the same dumps open on any host with the tools). DUMP-OR-ENVELOPE is a minidump (.dmp) or the .envelope file
# a crash leaves in the dump directory (the minidump is pulled out of it with crash_extract). The second argument is the
# program that crashed, or a symbol store made by scripts/store-symbols.sh (or both, repeated: pass the executable first).
#
# Prints the faulting thread's trace, every thread, and the modules with their build ids, using lldb; and, when it is
# installed, minidump-stackwalk (rust-minidump) for a symbolized trace from Breakpad symbol files. If you pass the program, its
# build id is compared with the one in the dump; a different build gives wrong symbols, so the script says so and exits 3.
# Exit status: 0 ok, 3 build id mismatch, 64 bad usage, 66 no tool available, 2 other failure.
# Environment: ENGINE_CRASH_EXTRACT is the path of the crash_extract tool (default: found in ./build or on PATH).
set -u

dump="${1:-}"
shift || true
if [ -z "$dump" ] || [ ! -f "$dump" ]; then
  echo "usage: crash-report.sh DUMP-OR-ENVELOPE [EXECUTABLE] [SYMBOL-STORE]" >&2
  exit 64
fi
exe=""
store=""
for arg in "$@"; do
  if [ -d "$arg" ]; then store="$arg"; elif [ -f "$arg" ]; then exe="$arg"; else echo "crash-report.sh: no such file or directory: $arg" >&2; exit 64; fi
done

# Tools first (bash builtins only, so this also works with an empty PATH): without a debugger there is nothing to run.
have_lldb=0
have_stackwalk=0
command -v lldb >/dev/null 2>&1 && have_lldb=1
command -v minidump-stackwalk >/dev/null 2>&1 && have_stackwalk=1
if [ $have_lldb -eq 0 ] && [ $have_stackwalk -eq 0 ]; then
  printf '%s\n' "crash-report.sh: no tool to read a minidump was found. Install lldb (your package manager: \`lldb\`), or" \
    "minidump-stackwalk (\`cargo install minidump-stackwalk\`), and run this again." >&2
  exit 66
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/crash-report.XXXXXX")" || exit 2
trap 'rm -rf "$work"' EXIT

# An envelope holds the minidump; a minidump starts with "MDMP".
magic="$(head -c 4 "$dump" 2>/dev/null)"
if [ "$magic" != "MDMP" ]; then
  extract="${ENGINE_CRASH_EXTRACT:-}"
  if [ -z "$extract" ]; then
    for candidate in ./build/tools/crash_extract/crash_extract "$(dirname "${BASH_SOURCE[0]}")/../build/tools/crash_extract/crash_extract"; do
      [ -x "$candidate" ] && extract="$candidate" && break
    done
  fi
  [ -z "$extract" ] && extract="$(command -v crash_extract 2>/dev/null || true)"
  if [ -z "$extract" ] || [ ! -x "$extract" ]; then
    echo "crash-report.sh: $dump is not a minidump, and the crash_extract tool was not found (build the project, or set ENGINE_CRASH_EXTRACT)" >&2
    exit 2
  fi
  if ! "$extract" "$dump" "$work/crash.dmp" >/dev/null; then
    echo "crash-report.sh: no minidump could be extracted from $dump" >&2
    exit 2
  fi
  dump="$work/crash.dmp"
fi

lldb_script=(-o 'thread list' -o 'bt all' -o 'image list')

if [ $have_lldb -eq 1 ]; then
  # No debuginfod: finding symbols must not go to the network.
  # The core is loaded first; the settings must come before the modules are loaded, so they go in a startup file.
  startup="$work/lldbinit"
  : > "$startup"
  # lldb command arguments are quoted with double quotes; a backslash or quote inside a path is escaped.
  lldb_quote() { local q="${1//\\/\\\\}"; printf '"%s"' "${q//\"/\\\"}"; }
  if [ -n "$store" ]; then
    echo "settings set target.debug-file-search-paths $(lldb_quote "$store")" >> "$startup"
  fi
  if [ -n "$exe" ]; then
    echo "settings set target.exec-search-paths $(lldb_quote "$(dirname "$(readlink -f "$exe")")")" >> "$startup"
  fi
  out="$(DEBUGINFOD_URLS= lldb --no-lldbinit -S "$startup" -b -c "$dump" "${lldb_script[@]}" 2>&1)"
  lldb_status=$?
  if [ $lldb_status -ne 0 ] || ! printf '%s\n' "$out" | grep -q '^ *\*\? *thread #[0-9]'; then
    printf '%s\n' "$out" >&2
    echo "crash-report.sh: lldb could not read $dump as a crash dump (see its output above)" >&2
    exit 2
  fi
  # The module that crashed: the one whose file name is the program's (when the program is given), else the first listed.
  # Its UUID is the build id (16 bytes, then an age) in GUID form.
  modules="$(printf '%s\n' "$out" | grep '^\[ *[0-9]*\] [0-9A-Fa-f-]')"
  line=""
  if [ -n "$exe" ]; then
    line="$(printf '%s\n' "$modules" | awk -v name="/$(basename "$exe")" '{ if (index($0, name) > 0 && index($0, name) + length(name) - 1 == length($0)) { print; exit } }')"
  fi
  [ -z "$line" ] && line="$(printf '%s\n' "$modules" | head -n 1)"
  got="$(printf '%s\n' "$line" | sed -n 's/^\[ *[0-9]*\] \([0-9A-Fa-f-]*\) .*/\1/p' | tr -d '-' | tr 'A-F' 'a-f')"
  if [ -n "$exe" ]; then
    want="$(readelf -n "$exe" 2>/dev/null | sed -n 's/.*Build ID: *//p' | head -n 1 | tr 'A-F' 'a-f')"
    if [ -n "$want" ] && [ -n "$got" ] && [ "${got:0:32}" != "${want:0:32}" ]; then
      echo "crash-report.sh: the dump was made by a different build than $exe (dump: ${got:0:32}, program: ${want:0:32}); the trace would be wrong" >&2
      exit 3
    fi
    [ -z "$want" ] && echo "crash-report.sh: $exe has no build id; it cannot be checked against the dump" >&2
  fi
  echo "== faulting thread and every thread (lldb) =="
  printf '%s\n' "$out"
fi

if [ -n "$exe" ] && [ $have_lldb -eq 0 ]; then
  echo "crash-report.sh: warning: the build id of $exe was not compared with the dump (that check needs lldb); make sure it is the program that crashed" >&2
fi
if [ $have_stackwalk -eq 1 ]; then
  echo "== symbolized trace (minidump-stackwalk) =="
  if [ -n "$store" ] && [ -d "$store/breakpad" ]; then
    minidump-stackwalk --symbols-path "$store/breakpad" "$dump"
  else
    minidump-stackwalk "$dump"
  fi
fi
exit 0
