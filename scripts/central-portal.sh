#!/usr/bin/env bash
set -euo pipefail

action=${1:?usage: central-portal.sh upload|status|publish|drop ARGUMENT}
argument=${2:?usage: central-portal.sh upload|status|publish|drop ARGUMENT}
base=https://central.sonatype.com/api/v1/publisher

[ -n "${MAVEN_CENTRAL_USERNAME:-}" ] || { echo "MAVEN_CENTRAL_USERNAME is required" >&2; exit 1; }
[ -n "${MAVEN_CENTRAL_PASSWORD:-}" ] || { echo "MAVEN_CENTRAL_PASSWORD is required" >&2; exit 1; }
authorization=$(printf '%s:%s' "$MAVEN_CENTRAL_USERNAME" "$MAVEN_CENTRAL_PASSWORD" | base64 | tr -d '\n')

# Every call is bounded: 15 s to connect, retries for transient network
# failures, and a hard per-request deadline (uploads get longer to move
# the multi-megabyte bundle) so a hung request fails the release job
# instead of occupying the runner indefinitely.
CURL_LIMITS=(--connect-timeout 15 --retry 3 --retry-connrefused --max-time 120)
CURL_UPLOAD_LIMITS=(--connect-timeout 15 --retry 3 --retry-connrefused --max-time 900)

case "$action" in
    upload)
        [ -f "$argument" ] || { echo "Central bundle does not exist: $argument" >&2; exit 1; }
        curl --fail --silent --show-error \
            "${CURL_UPLOAD_LIMITS[@]}" \
            --request POST \
            --header "Authorization: Bearer $authorization" \
            --form "bundle=@$argument" \
            "$base/upload?name=tex-core-$(cat VERSION)&publishingType=USER_MANAGED"
        ;;
    status)
        curl --fail --silent --show-error \
            "${CURL_LIMITS[@]}" \
            --request POST \
            --header "Authorization: Bearer $authorization" \
            "$base/status?id=$argument"
        ;;
    publish)
        curl --fail --silent --show-error \
            "${CURL_LIMITS[@]}" \
            --request POST \
            --header "Authorization: Bearer $authorization" \
            "$base/deployment/$argument"
        ;;
    drop)
        curl --fail --silent --show-error \
            "${CURL_LIMITS[@]}" \
            --request DELETE \
            --header "Authorization: Bearer $authorization" \
            "$base/deployment/$argument"
        ;;
    *)
        echo "unknown Central Portal action: $action" >&2
        exit 2
        ;;
esac
