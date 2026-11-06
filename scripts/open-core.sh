#!/usr/bin/env bash
# Opens the newest core of a program in gdb (batch mode) and prints the backtrace of every thread.
#
#   scripts/open-core.sh [--pid PID] [--wait SECONDS] EXECUTABLE [CORE]
#
# Linux only (a shell script). Without CORE it asks scripts/find-core.sh for the newest one, which also checks the build id.
# gdb is told not to use debuginfod (no network) unless ENGINE_ALLOW_DEBUGINFOD is set.
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
if ! command -v gdb >/dev/null 2>&1; then
  echo "open-core.sh: gdb is not installed" >&2
  exit 127
fi
if [ -z "$core" ]; then
  core="$("$here/find-core.sh" "${find_args[@]}" "$exe")" || exit $?
fi
if [ -z "${ENGINE_ALLOW_DEBUGINFOD:-}" ]; then
  export DEBUGINFOD_URLS=
fi
exec gdb -batch -nx -ex bt -ex 'thread apply all bt' "$exe" "$core"
