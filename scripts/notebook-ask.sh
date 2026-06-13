#!/usr/bin/env bash
# Quick query against the project's NotebookLM notebook. Avoids loading
# large specs (MAVLink, ArduPilot, libcamera) into the conversation context.
#
# Usage:
#   ./scripts/notebook-ask.sh "What is the SET_POSITION_TARGET_LOCAL_NED type_mask layout?"
#
# Notebook contents (as of S2):
#   - CLAUDE_CODE_PROMPT_NEW.md  (full project spec)
#   - PROJECT_MEMORY.md / DECISIONS.md / ROADMAP.md / SESSION_LOG.md
#   - MAVLink Common Messages
#   - MAVLink Serialization (framing v1/v2, CRC)
#   - ArduPilot Companion Computers / GUIDED mode / commands-in-GUIDED
#   - libcamera C++ API
#   - PGM netpbm format spec
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 \"<question>\"" >&2
  exit 1
fi

python -m notebooklm ask "$*"
