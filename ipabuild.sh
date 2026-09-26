#!/usr/bin/env bash
set -euo pipefail
exec bash "$(dirname "$0")/scripts/build_sjz.sh" "$@"
