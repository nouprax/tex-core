# Shared checksum, manifest, and extraction helpers for the CI artifact
# adapter scripts. This file is sourced by bash scripts that already run
# with `set -euo pipefail`.
#
# Stock macOS provides `shasum` but not `sha256sum`; both tools use the same
# `<hash>  <file>` manifest format, so artifacts stay byte-compatible across
# hosts.

# artifact_sha256_write <directory> <file>...
artifact_sha256_write() {
    local directory=$1
    shift
    (
        cd "$directory"
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum "$@" >SHA256SUMS
        else
            shasum -a 256 "$@" >SHA256SUMS
        fi
    )
}

# artifact_sha256_check <directory>
artifact_sha256_check() {
    (
        cd "$1"
        if command -v sha256sum >/dev/null 2>&1; then
            sha256sum --check SHA256SUMS
        else
            shasum -a 256 --check SHA256SUMS
        fi
    )
}

# artifact_source_sha <root>
artifact_source_sha() {
    echo "${GITHUB_SHA:-$(git -C "$1" rev-parse HEAD)}"
}

# artifact_verify <artifact-dir> <kind>
artifact_verify() {
    local artifact_dir=$1
    local kind=$2
    test -d "$artifact_dir"
    artifact_sha256_check "$artifact_dir"
    grep -Fxq "kind=$kind" "$artifact_dir/manifest.txt"
    if [ -n "${GITHUB_SHA:-}" ]; then
        grep -Fxq "source_sha=$GITHUB_SHA" "$artifact_dir/manifest.txt"
    fi
}

# artifact_extract <artifact-dir> <tarball> <destination>
artifact_extract() {
    tar -xzf "$1/$2" -C "$3"
}
