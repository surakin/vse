#!/usr/bin/env bash
# Formats the engine's C and C++ sources with clang-format (.clang-format; files in .clang-format-ignore are skipped).
#
#   scripts/format.sh            format every tracked source file in place
#   scripts/format.sh --check    change nothing; list the files that are not formatted and exit 1 if there are any
#   scripts/format.sh FILE...    format (or --check) just these files
#
# clang-format does the layout; scripts/wrap_comments.py then breaks comment lines longer than the column limit (clang-format's own comment reflow
# would join doc-comment labels onto the line before). clang-format runs again after it, and until the output is stable (it needs a second pass now and then).
#
# The style is in CLAUDE.md ("Code style"); the pre-commit hook (scripts/install-git-hooks.sh) runs this on the staged files.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

check=0
files=()
for arg in "$@"; do
    case "$arg" in
        --check) check=1 ;;
        -h|--help) sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "format: unknown option $arg" >&2; exit 2 ;;
        *) files+=("$arg") ;;
    esac
done

if ! command -v clang-format >/dev/null 2>&1; then
    echo "format: clang-format is not installed (BUILD.md lists it; version 18 or newer)" >&2
    exit 2
fi
version="$(clang-format --version | sed -n 's/.*version \([0-9]*\)\..*/\1/p' | head -n 1)"
if [ "${version:-0}" -lt 18 ]; then
    echo "format: clang-format $version is too old (18 or newer: .clang-format-ignore and InsertBraces need it)" >&2
    exit 2
fi

if [ ${#files[@]} -eq 0 ]; then
    while IFS= read -r f; do files+=("$f"); done < <(git ls-files '*.h' '*.hpp' '*.cpp' '*.cc' '*.c' '*.mm' '*.m')
fi
[ ${#files[@]} -gt 0 ] || exit 0

jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

run_format() {
    printf '%s\0' "${files[@]}" | xargs -0 -n 40 -P "$jobs" clang-format -i
}
run_wrap() {
    if command -v python3 >/dev/null 2>&1; then
        printf '%s\0' "${files[@]}" | xargs -0 -n 200 python3 "$root/scripts/wrap_comments.py" "$@" >/dev/null
    else
        echo "format: python3 not found, long comments were not wrapped" >&2
    fi
}

if [ "$check" = 1 ]; then
    # --dry-run -Werror makes clang-format fail for a file that would change; the wrapper's --check lists files with a comment that is too long.
    report="$(mktemp)"
    trap 'rm -f "$report"' EXIT
    status=0
    if ! printf '%s\0' "${files[@]}" | xargs -0 -n 40 -P "$jobs" clang-format --dry-run -Werror 2>"$report" >/dev/null; then
        grep -E '^[^ ]+:[0-9]+:[0-9]+: error' "$report" | cut -d: -f1 | sort -u | sed 's/^/not formatted: /' >&2 || true
        status=1
    fi
    if command -v python3 >/dev/null 2>&1; then
        printf '%s\0' "${files[@]}" | xargs -0 -n 200 python3 "$root/scripts/wrap_comments.py" --check || status=1
    fi
    if [ "$status" != 0 ]; then
        echo "format: run scripts/format.sh" >&2
        exit 1
    fi
    echo "format: ${#files[@]} files formatted"
else
    run_format
    run_wrap
    for _ in 1 2 3; do
        run_format
        if printf '%s\0' "${files[@]}" | xargs -0 -n 40 -P "$jobs" clang-format --dry-run -Werror >/dev/null 2>&1; then
            break
        fi
    done
    echo "format: ${#files[@]} files"
fi
