#!/usr/bin/env bash
# Sync a project doc into the NotebookLM notebook after a milestone is
# finalized. Used at session end so the notebook always reflects current
# committed state.
#
# Idempotent: each doc is keyed by its basename as the source title. A re-sync
# deletes EVERY existing source carrying that title (matched from `source list
# --json`, removed by ID) and then adds the current file once — so repeated
# milestone syncs replace the doc instead of piling up duplicate copies.
#
# Why delete by ID and not `delete-by-title`: the CLI's delete-by-title
# deliberately refuses when a title matches more than one source ("Delete by ID
# instead", exit 1), which is exactly the duplicate case we need to clean up.
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

  # Collect IDs of every existing source whose exact title equals this file's
  # basename. One list call per file keeps the logic simple; the notebook is
  # small. A non-zero list (e.g. transient auth) yields no IDs -> we just add.
  ids="$(python -m notebooklm source list --json 2>/dev/null \
    | python -c "import sys,json;d=json.load(sys.stdin);[print(s['id']) for s in d.get('sources',[]) if s.get('title')==sys.argv[1]]" \
      "$title" 2>/dev/null || true)"

  for id in $ids; do
    python -m notebooklm source delete "$id" -y >/dev/null 2>&1 || true
  done

  python -m notebooklm source add "$path" --title "$title"
done
