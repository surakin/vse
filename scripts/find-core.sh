#!/usr/bin/env bash
# Finds the newest core dump of a program and checks that it was made by this build of the program.
#
#   scripts/find-core.sh [--pid PID] [--wait SECONDS] [--output FILE] EXECUTABLE
#
# Linux and macOS (Windows dumps are handled by the Phase 13 tooling). Linux: with systemd-coredump (a core_pattern that
# starts with `|` and names it) the core is fetched with `coredumpctl dump`; otherwise it is the newest file matching the
# core_pattern in the current directory. The core's executable build id is compared with EXECUTABLE's; a core from a
# different build exits with status 3. On success the core's path is printed on standard output and the debugger command
# on standard error. Exit status: 0 found, 2 not found, 3 build id mismatch, 64 bad usage.
#
# macOS (Phase 26 Day 308): the pattern is `sysctl kern.corefile` (normally /cores/core.%P), and cores are written only
# when kern.coredump is 1, the core limit is above 0 and /cores is writable by the user (it belongs to root: `sudo chmod
# 1777 /cores` allows it). The build id is the LC_UUID, read from the core with lldb; the command printed is lldb's.
# ENGINE_CORE_PATTERN, on either system, replaces the system's pattern (for cores moved elsewhere).
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
# The absolute path with every link resolved (realpath: coreutils on Linux, part of macOS since 13; crash-report.sh's
# real_dir resolves the same way).
exe="$(realpath "$exe" 2>/dev/null || printf '%s/%s' "$(cd "$(dirname "$exe")" && pwd -P)" "$(basename "$exe")")"
darwin=0
[ "$(uname -s 2>/dev/null)" = "Darwin" ] && darwin=1
if [ -n "${ENGINE_CORE_PATTERN:-}" ]; then
  pattern="$ENGINE_CORE_PATTERN"
elif [ $darwin -eq 1 ]; then
  pattern="$(sysctl -n kern.corefile 2>/dev/null || true)"
else
  pattern="$(cat /proc/sys/kernel/core_pattern 2>/dev/null || true)"
fi
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
  local name glob
  name="$(basename "$exe")"
  if [ $darwin -eq 1 ]; then
    # macOS: %N the process name, %P the pid, %U the user id. The kernel's name is p_name, cut to 32 bytes (2 * MAXCOMLEN;
    # xnu's proc_best_name), as Linux cuts %e to 15; not seen in a kernel core here (KNOWN-ISSUES).
    glob="${pattern//%N/${name:0:32}}"
    [ -n "$pid" ] && glob="${glob//%P/$pid}"
  else
    name="${name:0:15}" # %e is the kernel's 15-character name of the program
    glob="${pattern//%e/$name}"
    [ -n "$pid" ] && glob="${glob//%p/$pid}"
  fi
  # The remaining specifiers become wildcards. The pattern is split into words by nothing (IFS is empty), so a directory
  # with spaces works.
  glob="${glob//%[a-zA-Z%]/*}"
  local IFS=
  # shellcheck disable=SC2086
  core="$(ls -1td $glob 2>/dev/null | head -n 1)"
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
  if [ $darwin -eq 1 ]; then
    hint="kern.corefile: ${pattern:-unreadable}, kern.coredump: $(sysctl -n kern.coredump 2>/dev/null || echo unreadable)"
    dir="$(dirname "$pattern")"
    [ -d "$dir" ] && [ ! -w "$dir" ] && hint="$hint; $dir is not writable by you (sudo chmod 1777 $dir)"
    echo "find-core.sh: no core found for $exe ($hint; is 'ulimit -c' above 0 in the shell that started it?)" >&2
  else
    echo "find-core.sh: no core found for $exe (core_pattern: ${pattern:-unreadable}; is 'ulimit -c' above 0 in the shell that started it?)" >&2
  fi
  exit 2
fi

# The build id of the executable inside the core, against the build id of the file given.
if [ $darwin -eq 1 ]; then
  # The LC_UUIDs of the program (one per architecture) and the one the core records for it. lldb is opened on the core
  # alone (given the program, it would use that file whatever the core says): it lists an image it could load as
  # "[N] UUID ADDRESS PATH", and one it could not as "Unable to find file PATH with UUID UUID at address ...". The
  # program's is the one whose path ends with its name, else the first image lldb loaded.
  wants="$(dwarfdump --uuid "$exe" 2>/dev/null | sed -n 's/^UUID: \([0-9A-F-]*\) .*/\1/p')"
  have=""
  if command -v lldb >/dev/null 2>&1; then
    listing="$(lldb --no-lldbinit -b -O 'settings set symbols.enable-external-lookup false' -c "$core" -o 'image list' 2>&1)"
    # The loaded images first (lldb may end the line with the load address in parentheses), then the ones it could not
    # load, so the fallback is the program when lldb found it.
    images="$(printf '%s\n' "$listing" | sed -n -e 's/ *(0x[0-9a-fA-F]*) *$//' \
      -e 's/^\[ *[0-9]*\] \([0-9A-F-]*\) 0x[0-9a-fA-F]* \(.*[^ ]\) *$/\1 \2/p'
      printf '%s\n' "$listing" | sed -n 's/^Unable to find file \(.*\) with UUID \([0-9A-F-]*\) at address.*/\2 \1/p')"
    have="$(printf '%s\n' "$images" | awk -v name="/$(basename "$exe")" '{ path = substr($0, index($0, " ") + 1);
      if (length(path) >= length(name) && substr(path, length(path) - length(name) + 1) == name) { print $1; exit } }')"
    # No name matched (the program renamed after the crash): the first image that is not the system's own (dyld, a
    # /usr/lib or /System library), which is the program whether lldb could load it or not.
    [ -z "$have" ] && have="$(printf '%s\n' "$images" | awk '{ path = substr($0, index($0, " ") + 1) }
      path !~ /^\/(usr\/lib|System)\// { print $1; exit }')"
  fi
  if [ -n "$wants" ] && [ -n "$have" ]; then
    if ! printf '%s\n' "$wants" | grep -qx "$have"; then
      echo "find-core.sh: $core was made by a different build (core: $have, $exe: $(printf '%s' "$wants" | tr '\n' ' ')); its symbols will not match" >&2
      exit 3
    fi
  else
    echo "find-core.sh: build id not checked (lldb could not read the core, or the executable has no LC_UUID)" >&2
  fi
  echo "$core"
  echo "lldb -o bt -o 'bt all' -c '$core' '$exe'" >&2
  exit 0
fi
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
