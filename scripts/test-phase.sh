#!/usr/bin/env bash
# Runs the tests of one phase, of the current phase plus whatever a diff touched, or all of them (Phase 6).
#
#   scripts/test-phase.sh [N | --current | --affected | --all] [options]
#
#   N          one phase: 0 1 2 3 4 ... (every test of that phase)
#   --current  the phase in CURRENT_PHASE (the default)
#   --affected the current phase plus the phases of every module touched since the base (the working tree, new files and the
#              commits since the base), plus every side phase in SIDE_PHASES that this build has tests of (the summary marks it
#              "(side)"). The base is, in order: --since REF; the day start recorded by --start-day, if it is an ancestor of HEAD and
#              not older than the phase's start; the merge-base with the upstream branch, under the same rule; the last commit that
#              changed CURRENT_PHASE. The first line printed names the base and the rule that chose it.
#              Runs everything when a CMakeLists.txt, cmake/, CMakePresets.json, third_party/, a header under
#              engine/foundation, tests/phases.txt or tests/module_map.txt changed (the reasons are printed before anything is
#              built). A changed path that is in no line of tests/module_map.txt is an ERROR, not a reason to run everything: the
#              script stops before building and names the path; add its line to tests/module_map.txt (the ctest
#              test_phase_script_module_map_paths also fails when a source directory has no line). Exception: a CMakeLists.txt, tests/phases.txt or tests/module_map.txt whose
#              change only ADDS lines (a new library, a new test, a new map line) does not widen the run; for the root
#              CMakeLists.txt the added lines must be add_subdirectory calls or comments. (A new file is only added lines.)
#              The CMakeLists.txt of a directory under tests/, tools/ or game/ is never a reason (the map line of the directory names
#              the phases that exercise it; tools/docgen's goes to phase 3).
#   --all      every test; with --flavor all and nothing left out, also records how long it took in
#              <build>/.last-full-run
#
#   --flavor plain|asan|tsan|all   which sanitizer flavor (default plain: neither asan nor tsan)
#   --crashes                      include the tests labelled `crash` (the Phase 7 suite, which crashes processes on purpose and
#                                  so makes a desktop crash reporter notify); left out of every run but --all
#   --soak                         include the tests labelled `soak` (the long benches and stress runs, a minute or more each);
#                                  left out of every run but --all
#   --display / --no-display       include or leave out the window tests (label `display`); by default left out of
#                                  --current/--affected unless phase 1 is selected or engine/foundation changed
#   --jobs N                       parallel build jobs and tests (default: the number of CPUs; CMAKE_BUILD_PARALLEL_LEVEL, if set, wins for the build)
#   --build-dir DIR                the CMake build directory (default: build; on Windows build-msvc, the windows-msvc preset's; on
#                                  macOS build-macos, the macos-clang preset's)
#   --no-build                     do not build first (the build makes only the chosen flavors' targets, engine_tests_<flavor>)
#   --dry-run                      print what would run and stop
#   --since REF                    base commit for --affected
#   --start-day [REF]              record REF (default HEAD) as the day's start for --affected, in the checkout's own .git (never
#                                  committed; each worktree and machine has its own), and stop; run it when a day starts
#   --repo-root DIR                run git in DIR instead of this checkout; for tests
#   --report-long FILE             print the long-test warning for a ctest JUnit file (--output-junit) and stop; for tests
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
soak=auto
report_long=""
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
dry=0
do_build=1
since=""
start_day=0
start_ref=""
git_root=""
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
        --soak) soak=yes ;;
        --report-long) report_long="${2:?--report-long needs a JUnit file}"; shift ;;
        --display) display=yes ;;
        --no-display) display=no ;;
        --jobs) jobs="${2:?--jobs needs a number}"; shift ;;
        --build-dir) build="${2:?--build-dir needs a directory}"; shift ;;
        --no-build) do_build=0 ;;
        --dry-run) dry=1 ;;
        --since) since="${2:?--since needs a commit}"; shift ;;
        --start-day)
            start_day=1
            # An optional commit after it (default HEAD); anything that is not an option is taken as one.
            if [ $# -gt 1 ] && [ "${2#-}" = "$2" ]; then start_ref="$2"; shift; fi ;;
        --repo-root) git_root="${2:?--repo-root needs a directory}"; shift ;;
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
[ -n "$git_root" ] || git_root="$root"

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
    base_commit
    { git -C "$git_root" diff --name-only --no-renames "$diff_base" --
      git -C "$git_root" ls-files --others --exclude-standard; } | sort -u
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

# The base commit --affected compares with (set once, by base_commit; call it outside a $(...) so the choice is kept), and the
# rule that chose it (base_why). In order:
#   1. --since REF;
#   2. the day start recorded by --start-day (.git/test-phase-base), if it is an ancestor of HEAD and not older than the commit
#      that last changed CURRENT_PHASE (a stale record from an earlier phase cannot narrow a run);
#   3. the merge-base of HEAD and its upstream, under the same "not older" rule;
#   4. the last commit that changed CURRENT_PHASE (the start of the phase), else HEAD.
# The working tree and untracked files are always added to the diff.
diff_base=""
base_why=""
base_note=""
# The record's path in this checkout's own git directory (a worktree has its own). --git-path prints it relative to git_root unless it
# is absolute; --path-format=absolute would need git 2.31.
day_record() {
    local p
    p="$(git -C "$git_root" rev-parse --git-path test-phase-base 2>/dev/null)" || return 1
    case "$p" in /*|[A-Za-z]:*) ;; *) p="$git_root/$p" ;; esac
    printf '%s\n' "$p"
}
is_ancestor() { git -C "$git_root" merge-base --is-ancestor "$1" "$2" 2>/dev/null; }
base_commit() {
    [ -z "$diff_base" ] || return 0
    local phase_start record recorded upstream
    phase_start="$(git -C "$git_root" log -1 --format=%H -- CURRENT_PHASE 2>/dev/null || true)"
    if [ -n "$since" ]; then
        diff_base="$since"; base_why="--since"; return 0
    fi
    record="$(day_record)"
    if [ -n "$record" ] && [ -f "$record" ]; then
        recorded="$(tr -d '[:space:]' < "$record")"
        if ! git -C "$git_root" rev-parse -q --verify "$recorded^{commit}" >/dev/null 2>&1 || ! is_ancestor "$recorded" HEAD; then
            base_note="the recorded day start ${recorded:0:12} is not an ancestor of HEAD (a rebase or another branch); ignored"
        elif [ -n "$phase_start" ] && ! is_ancestor "$phase_start" "$recorded"; then
            base_note="the recorded day start ${recorded:0:12} is older than the phase's start; ignored"
        else
            diff_base="$recorded"; base_why="the day start recorded by --start-day"; return 0
        fi
    fi
    upstream="$(git -C "$git_root" merge-base HEAD '@{upstream}' 2>/dev/null || true)"
    if [ -n "$upstream" ] && { [ -z "$phase_start" ] || is_ancestor "$phase_start" "$upstream"; }; then
        diff_base="$upstream"; base_why="the merge-base with the upstream branch (no day start recorded: scripts/test-phase.sh --start-day)"
        if [ "$upstream" = "$(git -C "$git_root" rev-parse HEAD 2>/dev/null)" ]; then
            base_note="HEAD is pushed and no day start is recorded, so only the working tree counts: commits of this day that are already pushed are left out (record the day's start with --start-day REF)"
        fi
        return 0
    fi
    if [ -n "$phase_start" ]; then
        diff_base="$phase_start"; base_why="the last commit that changed CURRENT_PHASE (the start of the phase)"
    else
        diff_base=HEAD; base_why="HEAD (no commit changed CURRENT_PHASE)"
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
    if ! git -C "$git_root" ls-files --error-unmatch -- "$1" >/dev/null 2>&1; then
        return 0   # untracked: a new file
    fi
    local patch
    patch="$(git -C "$git_root" diff -U0 --no-renames "$diff_base" -- "$1")" || return 1
    if printf '%s\n' "$patch" | grep -q '^-[^-]'; then return 1; fi
    if [ "$1" = CMakeLists.txt ]; then
        if printf '%s\n' "$patch" | grep '^+[^+]' | grep -qvE '^\+[[:space:]]*(add_subdirectory\(|#|$)'; then return 1; fi
    fi
    return 0
}

forces_all() {
    case "$1" in
        tools/docgen/CMakeLists.txt) return 1 ;;
        # A test, tool or game directory's own CMakeLists.txt only defines that directory's targets and tests; the map line of the
        # directory names the phases that exercise it. (A module's, cmake/ and the root's still reach everything.) Without this a
        # single edited line in one test's CMake keeps every later --affected of the phase at the whole suite.
        tests/*/CMakeLists.txt|tools/*/CMakeLists.txt|game/*/CMakeLists.txt) return 1 ;;
        CMakeLists.txt|*/CMakeLists.txt|tests/phases.txt|tests/module_map.txt) only_additions "$1" && return 1; return 0 ;;
        cmake/*|CMakePresets.json|third_party/*) return 0 ;;
        engine/foundation/*.h) return 0 ;;
    esac
    return 1
}

# --start-day: record the commit the day starts from, for --affected; nothing is built or run.
if [ "$start_day" = 1 ]; then
    ref="${start_ref:-HEAD}"
    commit="$(git -C "$git_root" rev-parse -q --verify "$ref^{commit}" 2>/dev/null)" || die "--start-day: '$ref' is not a commit"
    record="$(day_record)" || die "--start-day: not in a git checkout ($git_root)"
    printf '%s\n' "$commit" > "$record"
    echo "test-phase: the day starts at ${commit:0:12} ($(git -C "$git_root" log -1 --format=%s "$commit")); --affected compares with it (recorded in $record)"
    exit 0
fi

# Warns about every test in a ctest JUnit file that ran long without the `soak` label: over 30 s, or 60 s for a sanitizer test
# (a name with asan or tsan as a word). A warning, not a failure: timing depends on the machine's load. The fix is a short form
# for the ordinary run, or the `soak` label for a test whose length is its point (docs/guide/building-and-testing.md, "Long tests").
long_test_warning() {
    local junit="$1" names
    [ -f "$junit" ] || return 0
    # The soak names go to awk in a file, not with -v: macOS's awk refuses a newline in a -v value, and -v also expands backslashes.
    names="$junit.soak"
    ctest --test-dir "$build" -N -L '^soak$' 2>/dev/null | sed -n 's/^ *Test *#[0-9]*: //p' > "$names" || true
    awk '
        FNR == NR { skip[$0] = 1; next }
        /<testcase / {
            name = $0; sub(/.*<testcase name="/, "", name); sub(/" classname=.*/, "", name)
            t = $0; sub(/.* time="/, "", t); sub(/".*/, "", t)
            gsub(/&quot;/, "\"", name); gsub(/&apos;/, "\047", name); gsub(/&lt;/, "<", name); gsub(/&gt;/, ">", name); gsub(/&amp;/, "\\&", name)
            limit = (name ~ /(^|[^a-z])(asan|tsan)([^a-z]|$)/) ? 60 : 30
            if (!(name in skip) && t + 0 > limit) printf "%8.1f s  %s\n", t, name
        }' "$names" "$junit" | sort -rn > "$junit.long" || true
    if [ -s "$junit.long" ]; then
        echo "test-phase: WARNING: these tests ran over 30 s (60 s for a sanitizer test) without the soak label; give each a short form or the label (docs/guide/building-and-testing.md, \"Long tests\"):"
        cat "$junit.long"
    fi
    rm -f "$junit.long" "$names"
}
if [ -n "$report_long" ]; then
    long_test_warning "$report_long"
    exit 0
fi

started=$SECONDS
full=0
reasons=""
unmapped=""
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
        if [ -z "$diff_file" ]; then
            base_commit
            [ -z "$base_note" ] || echo "test-phase: $base_note"
            echo "test-phase: --affected compares with ${diff_base:0:12}: $base_why"
        fi
        files="$(changed_files)"
        while IFS= read -r f; do
            [ -n "$f" ] || continue
            if forces_all "$f"; then full=1; reasons="$reasons $f"; continue; fi
            mp="$(module_phases "$f")"
            case "$mp" in
                '?') unmapped="$unmapped $f" ;;
                '-') ;;
                *) phases="$phases ${mp//,/ }" ;;
            esac
            case "$f" in engine/foundation/*|tests/foundation/*) platform_touched=1 ;; esac
        done <<< "$files"
        ;;
esac
if [ -n "$unmapped" ]; then
    die "these changed paths are in no line of tests/module_map.txt, so --affected cannot tell which phases they touch (nothing was built or run; it does not guess by running everything):$unmapped
add a line for each (the path, then the phases whose tests exercise it, as the other lines do), or use --all if you mean the full suite"
fi
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
# The soak tests (label `soak`: benches and stress runs whose length is their point) run with --all and --soak only.
show_soak=yes
if [ "$soak" = auto ] && [ "$mode" != all ]; then
    show_soak=no
    exclude="${exclude:+$exclude|}soak"
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
if [ -n "$reasons" ] && [ "$mode" = affected ]; then
    echo "test-phase: running everything because of:$reasons (the build starts now; --dry-run prints this without building)"
fi
# Tests are registered when their executable is built, so build before counting (a dry run counts the tree as built).
# Only the chosen flavors' targets are built (cmake/EngineFlavorTargets.cmake), in parallel: --jobs build jobs, unless
# CMAKE_BUILD_PARALLEL_LEVEL is set, which cmake --build reads itself.
case "$flavor" in
    all) build_targets=(engine_tests_plain engine_tests_asan); [ "$platform" = windows ] || build_targets+=(engine_tests_tsan) ;;
    *) build_targets=("engine_tests_$flavor") ;;
esac
build_parallel=()
[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ] || build_parallel=(--parallel "$jobs")
# The build step as words, for the run and for the dry run's report (tests/test_phase_script checks it).
build_plan="${build_parallel[*]+${build_parallel[*]} }--target ${build_targets[*]}"
if [ "$dry" = 0 ] && [ "$do_build" = 1 ]; then
    # A build system generated before the flavor targets existed has no rule for them until CMake runs again (a tree configured
    # from an older checkout keeps its engine_flavor_targets.txt, so the generated files are what is asked).
    if ! grep -qs "engine_tests_plain" "$build/Makefile" "$build/build.ninja"; then
        cmake "$build" > /dev/null || { echo "test-phase: re-running CMake in $build failed; nothing ran" >&2; exit 1; }
    fi
    cmake --build "$build" ${build_parallel[@]+"${build_parallel[@]}"} --target "${build_targets[@]}" ||
        { echo "test-phase: the build failed; nothing ran" >&2; exit 1; }
fi

# --affected adds each side phase this run would select tests of (the Mac builds phase 21's, the Linux machine does not), unless already
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

summary="phases: ${phases_shown}; flavors: ${flavor}${flavor_note}; display: $([ "$show_display" = yes ] && echo included || echo excluded); crash tests: $([ "$show_crashes" = yes ] && echo included || echo excluded); soak: $([ "$show_soak" = yes ] && echo included || echo excluded)"
if [ "$picked" -eq 0 ]; then
    echo "test-phase: the selection ($summary) contains no tests; nothing ran (a phase has none until its first test file is in tests/phases.txt; --affected adds the touched modules' phases)" >&2
    exit 1
fi
if [ "$dry" = 1 ]; then
    echo "would build: cmake --build $build $build_plan"
    echo "would run $picked of $total tests ($summary)"
    exit 0
fi

status=0
# Many Phase 7 tests crash a child on purpose. With a soft core limit of 0 the kernel does not hand those crashes to
# systemd-coredump, so a desktop crash reporter (Plasma's systray) does not raise a notification for each one. The hard limit
# stays as it was: the tests that need a real core raise the soft limit themselves.
ulimit -S -c 0 2>/dev/null || true
# This run's own JUnit file (the process id keeps two runs on one tree apart), read by the long-test warning and then removed, so a
# run that writes none cannot report an earlier run's tests.
junit="$build/Testing/test-phase-$$.xml"
mkdir -p "$build/Testing"
rm -f "$junit"
ctest --test-dir "$build" -j"$jobs" --output-on-failure --output-junit "$junit" ${sel[@]+"${sel[@]}"} || status=$?
elapsed=$((SECONDS - started))
long_test_warning "$junit"
rm -f "$junit"
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
