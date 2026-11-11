#!/usr/bin/env bash
# Runs the tests of one phase, of the current phase plus whatever a diff touched, or all of them (Phase 6).
#
#   scripts/test-phase.sh [N | --current | --affected | --all] [options]
#
#   N          one phase: 0 1 2 3 4 ... (every test of that phase)
#   --current  the phase in CURRENT_PHASE (the default)
#   --affected the current phase plus the phases of every module touched since that phase started (the working tree
#              and the commits since the last commit that changed CURRENT_PHASE; --since REF picks another base), plus every
#              side phase in SIDE_PHASES that this build has tests of (the summary marks it "(side)").
#              Runs everything when a CMakeLists.txt, cmake/, CMakePresets.json, third_party/, a header under
#              engine/foundation, tests/phases.txt or tests/module_map.txt changed, or when a changed path is in no
#              line of tests/module_map.txt. Exception: a CMakeLists.txt, tests/phases.txt or tests/module_map.txt whose
#              change only ADDS lines (a new library, a new test, a new map line) does not widen the run; for the root
#              CMakeLists.txt the added lines must be add_subdirectory calls or comments. (A new file is only added lines.)
#              tools/docgen/CMakeLists.txt is never a reason (it is the docs tool's; the map sends it to phase 3).
#   --all      every test; with --flavor all and nothing left out, also records how long it took in
#              <build>/.last-full-run
#
#   --flavor plain|asan|tsan|all   which sanitizer flavor (default plain: neither asan nor tsan)
#   --crashes                      include the tests labelled `crash` (the Phase 7 suite, which crashes processes on purpose and
#                                  so makes a desktop crash reporter notify); left out of every run but --all
#   --display / --no-display       include or leave out the window tests (label `display`); by default left out of
#                                  --current/--affected unless phase 1 is selected or engine/foundation changed
#   --jobs N                       parallel tests (default: the number of CPUs)
#   --build-dir DIR                the CMake build directory (default: build; on Windows build-msvc, the windows-msvc preset's; on
#                                  macOS build-macos, the macos-clang preset's)
#   --no-build                     do not build first
#   --dry-run                      print what would run and stop
#   --since REF                    base commit for --affected
#   --diff-from-file FILE          read the changed paths from FILE (one per line) instead of git; for tests
#   --additive-from-file FILE      with --diff-from-file: the paths in FILE (a subset of the changed ones) only had lines added; for tests
#   --current-phase-file FILE      read the current phase from FILE instead of CURRENT_PHASE; for tests
#   --side-phases-file FILE        read the side phases from FILE instead of SIDE_PHASES; for tests
#
# The last line says how much ran: "ran X of Y tests (phases: ...; flavors: ...; display: ...)".
# Closing a phase means --all --flavor all (the full suite, three flavors). See docs/guide/building-and-testing.md.
# On Windows run it with Git for Windows' bash (from a plain PowerShell: & "C:\Program Files\Git\bin\bash.exe" scripts/test-phase.sh).
# MSVC has no ThreadSanitizer, so there --flavor all means plain and asan, and the summary line says so; --flavor tsan is refused.
# On macOS it runs under the system's /bin/bash (3.2): keep to features that version has (no associative arrays, mapfile, ${x,,}).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) platform=windows ;;
    Darwin) platform=macos ;;
    *) platform=linux ;;
esac
case "$platform" in
    windows) build="$root/build-msvc" ;;
    macos) build="$root/build-macos" ;;
    *) build="$root/build" ;;
esac
mode=current
phase_arg=""
flavor=plain
display=auto
crashes=auto
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
dry=0
do_build=1
since=""
diff_file=""
additive_file=""
phase_file="$root/CURRENT_PHASE"
side_file="$root/SIDE_PHASES"

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' | sed 's/^# \{0,1\}//'; }
die() { echo "test-phase: $*" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --current) mode=current ;;
        --affected) mode=affected ;;
        --all) mode=all ;;
        --flavor) flavor="${2:?--flavor needs plain, asan, tsan or all}"; shift ;;
        --crashes) crashes=yes ;;
        --display) display=yes ;;
        --no-display) display=no ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift ;;
        --build-dir) build="${2:?--build-dir needs a directory}"; shift ;;
        --no-build) do_build=0 ;;
        --dry-run) dry=1 ;;
        --since) since="${2:?--since needs a commit}"; shift ;;
        --diff-from-file) diff_file="${2:?--diff-from-file needs a file}"; shift ;;
        --additive-from-file) additive_file="${2:?--additive-from-file needs a file}"; shift ;;
        --current-phase-file) phase_file="${2:?--current-phase-file needs a file}"; shift ;;
        --side-phases-file) side_file="${2:?--side-phases-file needs a file}"; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "unknown option $1 (try --help)" ;;
        *) [ -z "$phase_arg" ] || die "only one phase can be given"; phase_arg="$1"; mode=phase ;;
    esac
    shift
done
case "$flavor" in plain|asan|tsan|all) ;; *) die "--flavor must be plain, asan, tsan or all (got '$flavor')" ;; esac
if [ "$platform" = windows ] && [ "$flavor" = tsan ]; then
    die "--flavor tsan: MSVC has no ThreadSanitizer; the tsan flavor runs on the Linux machine"
fi
flavor_note=""
[ "$platform" = windows ] && [ "$flavor" = all ] && flavor_note=" (plain and asan, no tsan on Windows)"  # no ';' in it: the summary's fields are ';'-separated
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
    case "$p" in ''|*[!0-9]*) die "'$p' in $phase_file is not a phase number (a whole number like 4)" ;; esac
    printf '%s' "$p"
}

# The side phases (one number per line; '#' comments): phases run beside the main line on another machine (CLAUDE.md).
side_phases() {
    [ -f "$side_file" ] || return 0
    local line
    sed 's/#.*$//' "$side_file" | tr -d ' \t\r' | while IFS= read -r line; do
        [ -n "$line" ] || continue
        case "$line" in *[!0-9]*) die "'$line' in $side_file is not a phase number" ;; esac
        printf '%s\n' "$line"
    done
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

# The base commit --affected compares with (set once, by base_commit).
diff_base=""
base_commit() {
    [ -z "$diff_base" ] || return 0
    diff_base="$since"
    if [ -z "$diff_base" ]; then
        diff_base="$(git -C "$root" log -1 --format=%H -- CURRENT_PHASE 2>/dev/null || true)"
        [ -n "$diff_base" ] || diff_base=HEAD
    fi
}

# True when the change to $1 only added lines (a new file counts), and, for the root CMakeLists.txt, only add_subdirectory calls,
# comments and blank lines. A widening rule that fires on every new test or module would defeat the phases (CLAUDE.md, "Tests run by phase").
only_additions() {
    if [ -n "$diff_file" ]; then
        [ -n "$additive_file" ] && grep -qxF "$1" "$additive_file"
        return
    fi
    base_commit
    if ! git -C "$root" ls-files --error-unmatch -- "$1" >/dev/null 2>&1; then
        return 0   # untracked: a new file
    fi
    local patch
    patch="$(git -C "$root" diff -U0 --no-renames "$diff_base" -- "$1")" || return 1
    if printf '%s\n' "$patch" | grep -q '^-[^-]'; then return 1; fi
    if [ "$1" = CMakeLists.txt ]; then
        if printf '%s\n' "$patch" | grep '^+[^+]' | grep -qvE '^\+[[:space:]]*(add_subdirectory\(|#|$)'; then return 1; fi
    fi
    return 0
}

forces_all() {
    case "$1" in
        tools/docgen/CMakeLists.txt) return 1 ;;
        CMakeLists.txt|*/CMakeLists.txt|tests/phases.txt|tests/module_map.txt) only_additions "$1" && return 1; return 0 ;;
        cmake/*|CMakePresets.json|third_party/*) return 0 ;;
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
# The flavor and the left-out labels, as ctest options; the phases are added after the side phases are known.
flavor_sel=()
exclude="" # labels to leave out
case "$flavor" in
    plain) exclude="asan|tsan" ;;
    asan) flavor_sel+=(-L '^asan$') ;;
    tsan) flavor_sel+=(-L '^tsan$') ;;
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
show_crashes=yes
if [ "$crashes" = auto ] && [ "$mode" != all ]; then
    show_crashes=no
    exclude="${exclude:+$exclude|}crash"
fi
[ -z "$exclude" ] || flavor_sel+=(-LE "^(${exclude})\$")

# A phase and a sanitizer flavor are two -L options, which ctest combines with AND only from CMake 3.29 on; an older ctest uses
# just the last one and would run every phase of the flavor while this script reports a scoped run. Refuse instead.
if [ "$phases" != all ] && { [ "$flavor" = asan ] || [ "$flavor" = tsan ]; }; then
    ctest_version="$(ctest --version | awk 'NR == 1 { print $3 }')"
    ctest_major="${ctest_version%%.*}"
    ctest_minor="${ctest_version#*.}"
    ctest_minor="${ctest_minor%%.*}"
    if [ "${ctest_major:-0}" -lt 3 ] || { [ "${ctest_major:-0}" -eq 3 ] && [ "${ctest_minor:-0}" -lt 29 ]; }; then
        die "a scoped --flavor $flavor run needs ctest 3.29 or newer (this is $ctest_version, which cannot combine two label filters); use --all, or --flavor plain"
    fi
fi

if [ "$platform" = windows ]; then
    [ -d "$build" ] || die "no build directory at $build (configure first: cmake --preset windows-msvc)"
elif [ "$platform" = macos ]; then
    [ -d "$build" ] || die "no build directory at $build (configure first: cmake --preset macos-clang)"
else
    [ -d "$build" ] || die "no build directory at $build (configure first: cmake -S . -B build)"
fi
# Tests are registered when their executable is built, so build before counting (a dry run counts the tree as built).
if [ "$dry" = 0 ] && [ "$do_build" = 1 ]; then
    cmake --build "$build" || { echo "test-phase: the build failed; nothing ran" >&2; exit 1; }
fi

# --affected adds each side phase this run would select tests of (the Mac builds phase 26's, the Linux machine does not), unless already
# there. Counted after the build (Catch2 tests are registered when their executable is built) and with the run's own flavor and left-out
# labels, so a side phase is never named in the summary when none of its tests run.
side_added=""
if [ "$mode" = affected ] && [ "$phases" != all ]; then
    side_list="$(side_phases)" || exit 2
    for p in $side_list; do
        case " $phases " in *" $p "*) continue ;; esac
        n="$(ctest --test-dir "$build" -N -L "^$(label_of "$p")\$" ${flavor_sel[@]+"${flavor_sel[@]}"} | awk '/^Total Tests:/ { print $3 }')"
        [ "${n:-0}" -gt 0 ] || continue
        side_added="$side_added $p"
        phases="$(printf '%s\n' $phases "$p" | sort -u -n | tr '\n' ' ')"
        phases="${phases% }"
    done
fi
# The phases as the summary shows them: a side phase added by --affected is marked.
phases_shown=""
for p in $phases; do
    case " $side_added " in *" $p "*) phases_shown="$phases_shown $p (side)" ;; *) phases_shown="$phases_shown $p" ;; esac
done
phases_shown="${phases_shown# }"

# The label expressions for ctest.
sel=()
if [ "$phases" != all ]; then
    re=""
    for p in $phases; do re="$re|$(label_of "$p")"; done
    sel+=(-L "^(${re#|})\$")
fi
sel+=(${flavor_sel[@]+"${flavor_sel[@]}"})
count() { ctest --test-dir "$build" -N "$@" | awk '/^Total Tests:/ { print $3 }'; }
total="$(count)"
picked="$(count ${sel[@]+"${sel[@]}"})"
[ -n "$total" ] && [ -n "$picked" ] || die "could not count tests in $build (is it configured?)"

summary="phases: ${phases_shown}; flavors: ${flavor}${flavor_note}; display: $([ "$show_display" = yes ] && echo included || echo excluded); crash tests: $([ "$show_crashes" = yes ] && echo included || echo excluded)"
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
# Many Phase 7 tests crash a child on purpose. With a soft core limit of 0 the kernel does not hand those crashes to
# systemd-coredump, so a desktop crash reporter (Plasma's systray) does not raise a notification for each one. The hard limit
# stays as it was: the tests that need a real core raise the soft limit themselves.
ulimit -S -c 0 2>/dev/null || true
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
