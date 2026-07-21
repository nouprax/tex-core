#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
output=${1:?usage: stage-maven-publications.sh OUTPUT_DIRECTORY MODE}
mode=${2:-host}
repository="$output/repository"

case "$output" in
    /*) ;;
    *) output="$root/$output"; repository="$output/repository" ;;
esac

rm -rf "$output"
mkdir -p "$repository"

common=(
    --no-daemon
    --no-build-cache
    --rerun-tasks
    --warning-mode=fail
    "-PreleaseRepositoryDir=$repository"
)
base_tasks=(
    :packages:kotlin-tex-core:publishKotlinMultiplatformPublicationToReleaseStagingRepository
    :packages:kotlin-tex-core:publishJvmPublicationToReleaseStagingRepository
    :packages:kotlin-tex-core:publishAndroidPublicationToReleaseStagingRepository
    :packages:kotlin-tex-core:android-runtime:publishReleasePublicationToReleaseStagingRepository
)

case "$mode" in
    linux-release)
        tasks=("${base_tasks[@]}" :packages:kotlin-tex-core:publishLinuxX64PublicationToReleaseStagingRepository)
        ;;
    macos-native)
        tasks=(
            :packages:kotlin-tex-core:publishMacosArm64PublicationToReleaseStagingRepository
            :packages:kotlin-tex-core:jvmJar
        )
        ;;
    host)
        if [ "$(uname -s)" = Darwin ]; then
            tasks=("${base_tasks[@]}" :packages:kotlin-tex-core:publishMacosArm64PublicationToReleaseStagingRepository)
        else
            tasks=("${base_tasks[@]}" :packages:kotlin-tex-core:publishLinuxX64PublicationToReleaseStagingRepository)
        fi
        ;;
    *)
        echo "unknown Maven staging mode: $mode" >&2
        exit 2
        ;;
esac

cd "$root"
scripts/gradle.sh "${common[@]}" "${tasks[@]}"

if [ "$mode" = macos-native ]; then
    jvm_jar=$(find packages/kotlin-tex-core/build/libs -maxdepth 1 -type f \
        -name '*-jvm-*.jar' ! -name '*-sources.jar' | head -n 1)
    [ -n "$jvm_jar" ] || { echo "macOS JVM payload JAR was not produced" >&2; exit 1; }
    cp "$jvm_jar" "$output/macos-jvm.jar"
fi

echo "Staged Maven publications in $output"
