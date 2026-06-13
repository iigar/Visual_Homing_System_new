#!/usr/bin/env bash
# Sync a project doc into the NotebookLM notebook after a milestone is
# finalized. Used at session end so the notebook always reflects current
# committed state.
#
# Usage:
#   ./scripts/notebook-add-doc.sh docs/PROJECT_MEMORY.md docs/SESSION_LOG.md
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <path> [<path> ...]" >&2
  exit 1
fi

for path in "$@"; do
  echo "=== $path ==="
  python -m notebooklm source add "$path"
done
