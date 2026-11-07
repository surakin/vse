#!/usr/bin/env bash
# Finds the newest core dump of a program on Linux and checks that it was made by this build of the program.
#
#   scripts/find-core.sh [--pid PID] [--wait SECONDS] [--output FILE] EXECUTABLE
#
# Linux only (a shell script; Windows dumps are handled by the Phase 13 tooling). With systemd-coredump (a core_pattern that
# starts with `|` and names it) the core is fetched with `coredumpctl dump`; otherwise it is the newest file matching the
# core_pattern in the current directory. The core's executable build id is compared with EXECUTABLE's; a core from a
# different build exits with status 3. On success the core's path is printed on standard output and the gdb command on
# standard error. Exit status: 0 found, 2 not found, 3 build id mismatch, 64 bad usage.
#
# A core holds the memory of the process that died (keys, passwords, user data): keep it private and delete it when done.
set -u

pid=""
wait_seconds=0
output=""
while [ $# -gt 0 ]; do
  case "$1" in
    --pid) pid="${2:-}"; shift 2 ;;
    --wait) wait_seconds="${2:-0}"; shift 2 ;;
    --output) output="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    --) shift; break ;;
    -*) echo "find-core.sh: unknown option $1" >&2; exit 64 ;;
    *) break ;;
  esac
done
exe="${1:-}"
if [ -z "$exe" ] || [ ! -f "$exe" ]; then
  echo "usage: find-core.sh [--pid PID] [--wait SECONDS] [--output FILE] EXECUTABLE" >&2
  exit 64
fi
exe="$(readlink -f "$exe")"
pattern="$(cat /proc/sys/kernel/core_pattern 2>/dev/null || true)"
core=""

scratch=""
cleanup() { [ -n "$scratch" ] && rm -rf "$scratch"; }
trap cleanup EXIT

find_with_coredumpctl() {
  local out="$output"
  if [ -z "$out" ]; then
    # Not given: a private directory (a core holds process memory), made once (not per --wait attempt) and removed when the
    # script ends unless the core is the answer.
    if [ -z "$scratch" ]; then
      scratch="$(mktemp -d "${TMPDIR:-/tmp}/find-core.XXXXXX")" || return 1
    fi
    out="$scratch/core"
    rm -f "$out" # a partial file from an earlier attempt
  fi
  local match="$exe"
  [ -n "$pid" ] && match="$pid"
  coredumpctl dump --no-pager -o "$out" "$match" >/dev/null 2>&1 && [ -s "$out" ] && core="$out"
}

find_with_file() {
  # Turn the pattern's % specifiers into wildcards and take the newest match.
  local name
  name="$(basename "$exe")"
  name="${name:0:15}" # %e is the kernel's 15-character name of the program
  local glob="${pattern//%e/$name}"
  [ -n "$pid" ] && glob="${glob//%p/$pid}"
  glob="${glob//%[a-zA-Z%]/*}"
  # shellcheck disable=SC2086
  core="$(ls -1t $glob 2>/dev/null | head -n 1)"
  [ -n "$core" ] && [ -n "$output" ] && cp "$core" "$output" && core="$output"
}

deadline=$(( $(date +%s) + wait_seconds ))
while :; do
  case "$pattern" in
    "|"*systemd-coredump*) command -v coredumpctl >/dev/null 2>&1 && find_with_coredumpctl ;;
    "|"*) echo "find-core.sh: core_pattern hands cores to a program ($pattern); look in its documentation for where they go" >&2; exit 2 ;;
    *) find_with_file ;;
  esac
  [ -n "$core" ] && break
  [ "$(date +%s)" -ge "$deadline" ] && break
  sleep 1
done

if [ -z "$core" ]; then
  echo "find-core.sh: no core found for $exe (core_pattern: ${pattern:-unreadable}; is 'ulimit -c' above 0 in the shell that started it?)" >&2
  exit 2
fi

# The build id of the executable inside the core, against the build id of the file given.
want="$(readelf -n "$exe" 2>/dev/null | sed -n 's/.*Build ID: *//p' | head -n 1)"
if command -v eu-unstrip >/dev/null 2>&1 && [ -n "$want" ]; then
  # The executable is the module whose file name ends with the program's base name; failing that, the first module listed.
  modules="$(eu-unstrip -n --core="$core" 2>/dev/null)"
  line="$(printf '%s\n' "$modules" | awk -v name="/$(basename "$exe")" 'index($NF, name) == length($NF) - length(name) + 1 {print; exit}')"
  [ -z "$line" ] && line="$(printf '%s\n' "$modules" | head -n 1)"
  have="$(printf '%s\n' "$line" | awk '{print $2}' | sed 's/@.*//')"
  if [ -n "$have" ] && [ "$have" != "$want" ]; then
    echo "find-core.sh: $core was made by a different build (core: $have, $exe: $want); its symbols will not match" >&2
    exit 3
  fi
else
  echo "find-core.sh: build id not checked (eu-unstrip or the executable's build id is missing)" >&2
fi

if [ -n "$scratch" ] && [ "$core" = "$scratch/core" ]; then
  keep="$scratch"
  scratch="" # the caller asked for no particular file: leave this one in its private directory
fi
echo "$core"
echo "gdb -ex bt -ex 'thread apply all bt' '$exe' '$core'" >&2
exit 0
