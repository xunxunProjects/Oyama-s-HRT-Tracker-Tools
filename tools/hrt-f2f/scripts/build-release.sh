#!/usr/bin/env bash
# Deprecated path kept for existing release instructions.
# Use tools/hrt-tool/scripts/build-release.sh instead.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec "$ROOT/tools/hrt-tool/scripts/build-release.sh" "$@"
