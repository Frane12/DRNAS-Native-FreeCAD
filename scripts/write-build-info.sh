#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/out"
{
  echo 'project=DRNAS Native FreeCAD Android (OCCT phase 1)'
  echo "occt_ref=${OCCT_REF:-V7_9_3}"
  echo "ndk_version=${NDK_VERSION:-27.2.12479018}"
  echo "android_api=${ANDROID_API:-26}"
  echo 'android_abi=arm64-v8a'
  echo 'compiler_cache=ccache and GitHub Actions cache'
  echo "build_run=${GITHUB_RUN_ID:-local}"
  echo "built_at_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$ROOT/out/build-info.txt"
