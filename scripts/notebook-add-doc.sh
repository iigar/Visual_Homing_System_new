#!/usr/bin/env bash
# Sync a project doc into the NotebookLM notebook after a milestone is
# finalized. Used at session end so the notebook always reflects current
# committed state.
#
# Idempotent: each doc is keyed by its basename as the source title. A re-sync
# first deletes every prior source carrying that title, then adds the current
# file once — so repeated milestone syncs replace the doc instead of piling up
# duplicate copies. (The earlier version called `source add` unconditionally,
# which left N stale copies after N milestones.)
#
# Runs via Git Bash on Windows: `python` + the `notebooklm` package live on the
# Windows side, NOT in WSL (WSL has neither). Do not invoke through `wsl`.
#
# Usage:
#   ./scripts/notebook-add-doc.sh docs/PROJECT_MEMORY.md docs/SESSION_LOG.md
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <path> [<path> ...]" >&2
  exit 1
fi

for path in "$@"; do
  title="$(basename "$path")"
  echo "=== $title ==="
  # Remove every existing copy with this title. delete-by-title exits non-zero
  # once no source matches, which ends the loop (and is the expected state on a
  # first-ever sync). A non-"not found" failure also just ends the loop; the
  # subsequent `add` will surface the real error.
  while python -m notebooklm source delete-by-title "$title" -y >/dev/null 2>&1; do
    :
  done
  python -m notebooklm source add "$path" --title "$title"
done
