#!/bin/sh
set -eu

case "${1:-}" in
    --check)
        swift_format_args="lint"
        ;;
    "")
        swift_format_args="--in-place"
        ;;
    *)
        echo "Usage: $0 [--check]" >&2
        exit 2
        ;;
esac

if [ ! -f Package.swift ]; then
    echo "note: no Swift sources yet; format:swift is a no-op until Phase 5 lands the Swift binding"
    exit 0
fi

EXPECTED_VERSION="6.3.0"
actual_version=$(swift format --version)
if [ "$actual_version" != "$EXPECTED_VERSION" ]; then
    echo "Expected swift-format $EXPECTED_VERSION, found $actual_version" >&2
    exit 1
fi

{
    printf '%s\0' Package.swift
    find packages/swift-tex-core \
        \( -type d \( -name '.build' -o -name 'Generated' \) -prune \) -o \
        \( -type f -name '*.swift' -print0 \)
} | xargs -0 swift format $swift_format_args --configuration .swift-format
