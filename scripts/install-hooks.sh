#!/usr/bin/env bash
# Install the CloudPlay git hooks from scripts/hooks/ into .git/hooks/.
#
# Run this once after cloning (hooks are not version-controlled by git itself):
#   ./scripts/install-hooks.sh
set -euo pipefail

REPO_ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
GIT_DIR="$(git -C "$REPO_ROOT" rev-parse --git-dir)"
[[ "$GIT_DIR" != /* ]] && GIT_DIR="$REPO_ROOT/$GIT_DIR"

SRC_DIR="$REPO_ROOT/scripts/hooks"
DEST_DIR="$GIT_DIR/hooks"

mkdir -p "$DEST_DIR"

shopt -s nullglob
count=0
for hook in "$SRC_DIR"/*; do
    name="$(basename "$hook")"
    install -m 0755 "$hook" "$DEST_DIR/$name"
    echo "installed hook: $name -> $DEST_DIR/$name"
    count=$((count + 1))
done

if [[ $count -eq 0 ]]; then
    echo "no hook templates found in $SRC_DIR" >&2
    exit 1
fi

echo "done ($count hook(s) installed)"
