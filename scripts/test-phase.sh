#!/usr/bin/env bash
# Runs the tests of one phase, of the current phase plus whatever a diff touched, or all of them (Phase 5).
#
#   scripts/test-phase.sh [N | --current | --affected | --all] [options]
#
#   N          one phase: 0 1 2 2.5 3 4 ... (every test of that phase)
#   --current  the phase in CURRENT_PHASE (the default)
#   --affected the current phase plus the phases of every module touched since that phase started (the working tree
#              and the commits since the last commit that changed CURRENT_PHASE; --since REF picks another base).
#              Runs everything when a CMakeLists.txt, cmake/, CMakePresets.json, third_party/, a header under
#              engine/foundation, tests/phases.txt or tests/module_map.txt changed, or when a changed path is in no
#              line of tests/module_map.txt.
#   --all      every test; with --flavor all and nothing left out, also records how long it took in
#              <build>/.last-full-run
#
#   --flavor plain|asan|tsan|all   which sanitizer flavor (default plain: neither asan nor tsan)
#   --display / --no-display       include or leave out the window tests (label `display`); by default left out of
#                                  --current/--affected unless phase 1 is selected or engine/foundation changed
#   --jobs N                       parallel tests (default: the number of CPUs)
#   --build-dir DIR                the CMake build directory (default: build)
#   --no-build                     do not build first
#   --dry-run                      print what would run and stop
#   --since REF                    base commit for --affected
#   --diff-from-file FILE          read the changed paths from FILE (one per line) instead of git; for tests
#   --current-phase-file FILE      read the current phase from FILE instead of CURRENT_PHASE; for tests
#
# The last line says how much ran: "ran X of Y tests (phases: ...; flavors: ...; display: ...)".
# Closing a phase means --all --flavor all (the full suite, three flavors). See docs/guide/building-and-testing.md.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build"
mode=current
phase_arg=""
flavor=plain
display=auto
jobs="$(nproc 2>/dev/null || echo 4)"
dry=0
do_build=1
since=""
diff_file=""
phase_file="$root/CURRENT_PHASE"

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }
die() { echo "test-phase: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --current) mode=current ;;
        --affected) mode=affected ;;
        --all) mode=all ;;
        --flavor) flavor="${2:?--flavor needs plain, asan, tsan or all}"; shift ;;
        --display) display=yes ;;
        --no-display) display=no ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift ;;
        --build-dir) build="${2:?--build-dir needs a directory}"; shift ;;
        --no-build) do_build=0 ;;
        --dry-run) dry=1 ;;
        --since) since="${2:?--since needs a commit}"; shift ;;
        --diff-from-file) diff_file="${2:?--diff-from-file needs a file}"; shift ;;
        --current-phase-file) phase_file="${2:?--current-phase-file needs a file}"; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option $1 (try --help)" ;;
        *) [ -z "$phase_arg" ] || die "only one phase can be given"; phase_arg="$1"; mode=phase ;;
    esac
    shift
done
case "$flavor" in plain|asan|tsan|all) ;; *) die "--flavor must be plain, asan, tsan or all (got '$flavor')" ;; esac
case "$jobs" in ''|*[!0-9]*) die "--jobs must be a number" ;; esac

map="$root/tests/phases.txt"
modules="$root/tests/module_map.txt"
[ -f "$map" ] || die "missing $map"
known_phases="$(awk '!/^[[:space:]]*(#|$)/ && $1 != "flavors" { print $2 }' "$map" | sort -u -n | tr '\n' ' ')"
is_known() { case " $known_phases" in *" $1 "*) return 0 ;; esac; return 1; }
label_of() { printf 'phase%s' "${1//./_}"; }

current_phase() {
    [ -f "$phase_file" ] || die "$phase_file is missing; it holds the current phase (see --help)"
    local p
    p="$(grep -v '^[[:space:]]*#' "$phase_file" | tr -d '[:space:]')"
    # A new phase has no tests yet, so it need not be in tests/phases.txt.
    case "$p" in ''|*[!0-9.]*|.*|*.|*.*.*) die "'$p' in $phase_file is not a phase number (like 4 or 2.5)" ;; esac
    printf '%s' "$p"
}

changed_files() {
    if [ -n "$diff_file" ]; then
        [ -f "$diff_file" ] || die "no such file: $diff_file"
        grep -v '^[[:space:]]*$' "$diff_file" || true
        return
    fi
    local base="$since"
    if [ -z "$base" ]; then
        base="$(git -C "$root" log -1 --format=%H -- CURRENT_PHASE 2>/dev/null || true)"
        [ -n "$base" ] || base=HEAD
    fi
    { git -C "$root" diff --name-only --no-renames "$base" --
      git -C "$root" ls-files --others --exclude-standard; } | sort -u
}

# Prints the phases for one changed file: a comma list, "-" (nothing depends on it) or "?" (not in the map).
module_phases() {
    awk -v f="$1" '
        !/^[[:space:]]*(#|$)/ {
            p = $1
            if ((f == p || index(f, p "/") == 1) && length(p) > best) { best = length(p); out = $2 }
        }
        END { print (best ? out : "?") }' "$modules"
}

forces_all() {
    case "$1" in
        CMakeLists.txt|*/CMakeLists.txt|cmake/*|CMakePresets.json|third_party/*|tests/phases.txt|tests/module_map.txt) return 0 ;;
        engine/foundation/*.h) return 0 ;;
    esac
    return 1
}

started=$SECONDS
full=0
reasons=""
phases=""
platform_touched=0
case "$mode" in
    all) full=1; reasons="--all" ;;
    phase)
        is_known "$phase_arg" || die "'$phase_arg' is not a known phase (known: ${known_phases% })"
        phases="$phase_arg" ;;
    current) phases="$(current_phase)" ;;
    affected)
        phases="$(current_phase)"
        [ -f "$modules" ] || die "missing $modules"
        files="$(changed_files)"
        while IFS= read -r f; do
            [ -n "$f" ] || continue
            if forces_all "$f"; then full=1; reasons="$reasons $f"; continue; fi
            mp="$(module_phases "$f")"
            case "$mp" in
                '?') full=1; reasons="$reasons $f(not in tests/module_map.txt)" ;;
                '-') ;;
                *) phases="$phases ${mp//,/ }" ;;
            esac
            case "$f" in engine/foundation/*|tests/foundation/*) platform_touched=1 ;; esac
        done <<< "$files"
        ;;
esac
phases="$(printf '%s\n' $phases | sort -u -n | tr '\n' ' ')"
phases="${phases% }"
if [ "$full" = 1 ]; then
    phases=all
fi

# The label expressions for ctest.
sel=()
if [ "$phases" != all ]; then
    re=""
    for p in $phases; do re="$re|$(label_of "$p")"; done
    sel+=(-L "^(${re#|})\$")
fi
exclude="" # labels to leave out
case "$flavor" in
    plain) exclude="asan|tsan" ;;
    asan) sel+=(-L '^asan$') ;;
    tsan) sel+=(-L '^tsan$') ;;
esac
show_display=yes
if [ "$display" = no ]; then
    show_display=no
elif [ "$display" = auto ] && [ "$full" = 0 ] && { [ "$mode" = current ] || [ "$mode" = affected ]; }; then
    case " $phases " in *" 1 "*) ;; *) [ "$platform_touched" = 1 ] || show_display=no ;; esac
fi
if [ "$show_display" = no ]; then
    exclude="${exclude:+$exclude|}display"
fi
[ -z "$exclude" ] || sel+=(-LE "^(${exclude})\$")

[ -d "$build" ] || die "no build directory at $build (configure first: cmake -S . -B build)"
# Tests are registered when their executable is built, so build before counting (a dry run counts the tree as built).
if [ "$dry" = 0 ] && [ "$do_build" = 1 ]; then
    cmake --build "$build" || { echo "test-phase: the build failed; nothing ran" >&2; exit 1; }
fi
count() { ctest --test-dir "$build" -N "$@" | awk '/^Total Tests:/ { print $3 }'; }
total="$(count)"
picked="$(count ${sel[@]+"${sel[@]}"})"
[ -n "$total" ] && [ -n "$picked" ] || die "could not count tests in $build (is it configured?)"

summary="phases: ${phases}; flavors: ${flavor}; display: $([ "$show_display" = yes ] && echo included || echo excluded)"
if [ -n "$reasons" ] && [ "$mode" = affected ]; then
    echo "test-phase: running everything because of:$reasons"
fi
if [ "$picked" -eq 0 ]; then
    echo "test-phase: the selection ($summary) contains no tests; nothing ran (a phase has none until its first test file is in tests/phases.txt; --affected adds the touched modules' phases)" >&2
    exit 1
fi
if [ "$dry" = 1 ]; then
    echo "would run $picked of $total tests ($summary)"
    exit 0
fi

status=0
ctest --test-dir "$build" -j"$jobs" --output-on-failure ${sel[@]+"${sel[@]}"} || status=$?
elapsed=$((SECONDS - started))
last=""
if [ "$mode" = all ] && [ "$flavor" = all ] && [ "$picked" = "$total" ] && [ "$status" = 0 ]; then
    echo "$elapsed" > "$build/.last-full-run"
elif [ -f "$build/.last-full-run" ]; then
    last="; the last full run took $(cat "$build/.last-full-run") s"
fi
if [ "$picked" -lt "$total" ]; then
    echo "test-phase: NOT the full suite: $((total - picked)) tests were left out. Closing a phase needs: scripts/test-phase.sh --all --flavor all"
fi
echo "ran $picked of $total tests ($summary) in ${elapsed} s${last}"
exit "$status"
