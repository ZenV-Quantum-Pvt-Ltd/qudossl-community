#!/bin/bash
# Install tracked git hooks for this repository.
# Run once after cloning:  ./scripts/install-hooks.sh
#
# This points git at the tracked .githooks/ directory so the pre-commit
# clang-format check runs on every commit. CI (lint.yml) is the authoritative
# gate; this hook is a local convenience to catch violations before push.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

if [ ! -d .git ]; then
    echo "error: not a git checkout (no .git directory at $REPO_ROOT)" >&2
    exit 1
fi

if [ ! -d .githooks ]; then
    echo "error: .githooks/ directory not found" >&2
    exit 1
fi

git config core.hooksPath .githooks
echo "Git hooks installed: core.hooksPath=.githooks"
echo "Pre-commit clang-format check is now active."
