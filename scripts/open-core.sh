#!/usr/bin/env bash
# Opens the newest core of a program in the debugger (batch mode) and prints the backtrace of every thread.
#
#   scripts/open-core.sh [--pid PID] [--wait SECONDS] EXECUTABLE [CORE]
#
# Linux: gdb. macOS (Phase 21 Day 292): lldb, the system's debugger. Without CORE it asks scripts/find-core.sh for the
# newest one, which also checks the build id. The debugger is told not to look for symbols on the network: gdb gets no
# debuginfod unless ENGINE_ALLOW_DEBUGINFOD is set, lldb's external symbol search (Spotlight, DebugSymbols) is turned off.
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
find_args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --pid|--wait) find_args+=("$1" "${2:-}"); shift 2 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) break ;;
  esac
done
exe="${1:-}"
core="${2:-}"
if [ -z "$exe" ]; then
  echo "usage: open-core.sh [--pid PID] [--wait SECONDS] EXECUTABLE [CORE]" >&2
  exit 64
fi
debugger=gdb
[ "$(uname -s 2>/dev/null)" = "Darwin" ] && debugger=lldb
if ! command -v "$debugger" >/dev/null 2>&1; then
  echo "open-core.sh: $debugger is not installed" >&2
  exit 127
fi
if [ -z "$core" ]; then
  # ${a[@]+...}: bash 3.2 (macOS) treats an empty array as unset under set -u.
  core="$("$here/find-core.sh" ${find_args[@]+"${find_args[@]}"} "$exe")" || exit $?
fi
if [ "$debugger" = "lldb" ]; then
  exec lldb --no-lldbinit -b -O 'settings set symbols.enable-external-lookup false' -c "$core" "$exe" -o bt -o 'bt all'
fi
if [ -z "${ENGINE_ALLOW_DEBUGINFOD:-}" ]; then
  export DEBUGINFOD_URLS=
fi
exec gdb -batch -nx -ex bt -ex 'thread apply all bt' "$exe" "$core"
