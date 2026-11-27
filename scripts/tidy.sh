#!/usr/bin/env bash
# Runs clang-tidy (.clang-tidy) over the engine's sources on a separate non-unity build tree, build/tidy, which exports compile_commands.json.
#
#   scripts/tidy.sh                 every .cpp under engine/, game/, tools/ and tests/ (a few minutes)
#   scripts/tidy.sh FILE...         just these files
#   scripts/tidy.sh --changed       the .cpp files changed since the phase started (the commits since CURRENT_PHASE last changed, and the working tree)
#   scripts/tidy.sh --fix           also apply the fixes clang-tidy offers (review the diff; then scripts/format.sh)
#   scripts/tidy.sh --summary       print a count per check instead of every warning
#
# Findings are fixed, or suppressed on their line with `// NOLINT(check-name): the reason` (CLAUDE.md, "Code style"). Not in the commit hook.
# Exit status 1 when there are findings. Files that only the other platforms compile (windows/, macos/, .mm) are left out. Linux only for now.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
build="$root/build/tidy"
fix=0
changed=0
summary=0
files=()
for arg in "$@"; do
    case "$arg" in
        --fix) fix=1 ;;
        --changed) changed=1 ;;
        --summary) summary=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "tidy: unknown option $arg" >&2; exit 2 ;;
        *) files+=("$arg") ;;
    esac
done

command -v run-clang-tidy >/dev/null 2>&1 || { echo "tidy: run-clang-tidy is not installed (BUILD.md; clang-tidy 18 or newer)" >&2; exit 2; }
if [ ! -f "$build/compile_commands.json" ]; then
    echo "tidy: configuring $build (non-unity, with compile_commands.json)" >&2
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENGINE_UNITY_BUILD=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
fi

if [ ${#files[@]} -eq 0 ]; then
    if [ "$changed" = 1 ]; then
        base="$(git log -1 --format=%H -- CURRENT_PHASE 2>/dev/null || echo HEAD)"
        while IFS= read -r f; do files+=("$f"); done < <({ git diff --name-only --no-renames "$base" --; git ls-files --others --exclude-standard; } | sort -u | grep -E '\.(cpp|cc)$' || true)
    else
        while IFS= read -r f; do files+=("$f"); done < <(git ls-files 'engine/*.cpp' 'game/*.cpp' 'tools/*.cpp' 'tests/*.cpp')
    fi
    # Not compiled on this machine, or test data.
    kept=()
    for f in "${files[@]}"; do
        case "$f" in
            */windows/*|*/macos/*|*_windows*|*_macos*|*/fixtures/*|third_party/*) ;;
            *) [ -f "$f" ] && kept+=("$f") ;;
        esac
    done
    files=("${kept[@]}")
fi
[ ${#files[@]} -gt 0 ] || { echo "tidy: no files"; exit 0; }

jobs="$(nproc 2>/dev/null || echo 4)"
args=(-p "$build" -quiet -j "$jobs")
[ "$fix" = 1 ] && args+=(-fix)
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
# run-clang-tidy takes regular expressions: anchor each path.
patterns=()
for f in "${files[@]}"; do patterns+=("^$root/$f\$"); done
run-clang-tidy "${args[@]}" "${patterns[@]}" >"$out" 2>&1 || true

# One line per distinct finding (a header is checked once per file that includes it).
mkdir -p "$build"
cp "$out" "$build/tidy-output.txt"   # the raw output of the last run, for looking at a finding's notes
findings="$(grep -E '^/.*: (warning|error): .*\[[A-Za-z0-9.,_-]+\]$' "$out" | sort -u || true)"
if [ -z "$findings" ]; then
    echo "tidy: ${#files[@]} files, no findings"
    exit 0
fi
if [ "$summary" = 1 ]; then
    printf '%s\n' "$findings" | sed -E 's/.*\[([A-Za-z0-9.,_-]+)\]$/\1/' | sort | uniq -c | sort -rn
else
    printf '%s\n' "$findings" | sed "s|$root/||"
fi
echo "tidy: ${#files[@]} files, $(printf '%s\n' "$findings" | wc -l) findings" >&2
exit 1
