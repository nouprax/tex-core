#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$root/scripts/lib/discover-toolchain.sh"
repository="$root/build/kotlin-consumer-repository"
gradle="$root/scripts/gradle.sh"
property="-Dmaven.repo.local=$repository"
maven_repository_arg=
publish=true

if [ "${1:-}" = "--repository" ]; then
    repository=${2:?--repository requires a staged Maven repository path}
    case "$repository" in
        /*) ;;
        *) repository="$root/$repository" ;;
    esac
    consumer_local_repository="$root/build/kotlin-consumer-maven-local"
    rm -rf "$consumer_local_repository"
    mkdir -p "$consumer_local_repository"
    property="-Dmaven.repo.local=$consumer_local_repository"
    maven_repository_arg="-Dtex.core.consumer.repository=file://$repository"
    publish=false
fi

cd "$root"
if [ "$publish" = true ]; then
    rm -rf "$repository"
    "$gradle" --warning-mode=fail "$property" :packages:kotlin-tex-core:publishKotlinToMavenLocal
fi

"$gradle" --warning-mode=fail "$property" "-PconsumerRepository=$repository" \
    -p packages/kotlin-tex-core/consumers/kmp jvmTest
"$gradle" --warning-mode=fail "$property" "-PconsumerRepository=$repository" \
    -p packages/kotlin-tex-core/consumers/jvm-gradle run
"$gradle" --warning-mode=fail "$property" "-PconsumerRepository=$repository" \
    -p packages/kotlin-tex-core/consumers/android assembleDebug
MAVEN_USER_HOME="$root/build/maven-user-home" \
    MAVEN_OPTS="${MAVEN_OPTS:+$MAVEN_OPTS }--enable-native-access=ALL-UNNAMED" \
    "$root/mvnw" --batch-mode --no-transfer-progress \
    "$property" \
    ${maven_repository_arg:+"$maven_repository_arg"} \
    -f packages/kotlin-tex-core/consumers/jvm-maven/pom.xml \
    verify
