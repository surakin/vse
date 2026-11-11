#!/usr/bin/env bash
# Points git at the hooks kept in this repo (scripts/git-hooks/), so every checkout formats staged code before a commit.
# Run once on each machine, from anywhere inside the repo:  scripts/install-git-hooks.sh   (on Windows, from Git Bash)
# Also tells `git blame` to skip the commits listed in .git-blame-ignore-revs (formatting-only commits).
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
git config core.hooksPath scripts/git-hooks
chmod +x scripts/git-hooks/* scripts/format.sh 2>/dev/null || true
if [ -f .git-blame-ignore-revs ]; then
    git config blame.ignoreRevsFile .git-blame-ignore-revs
fi
echo "installed: core.hooksPath = scripts/git-hooks"
if ! command -v clang-format >/dev/null 2>&1; then
    echo "warning: clang-format is not installed; the pre-commit hook will refuse commits that stage C or C++ files until it is (BUILD.md)." >&2
fi
