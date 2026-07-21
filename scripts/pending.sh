#!/bin/sh
set -eu

phase=${1:?usage: scripts/pending.sh '<phase that lands this lane>'}
lane=${npm_lifecycle_event:-this lane}
echo "error: '$lane' has not landed yet; it arrives with $phase per docs/specs/2026-07-20-repo-setup.md" >&2
exit 1
