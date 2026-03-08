#!/bin/bash
# Required variable: PROJECT
# Assumes the project is already built.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"
cd ../src

# Check PROJECT
if [ -z "${PROJECT:-}" ]; then
    echo "PROJECT is not set!"
    exit 1
fi

PLATFORM=$(../scripts/get-native-arch.sh)

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -f "$VSWHERE" ]; then
    echo "Could not find vswhere.exe!"
    exit 1
fi

VSPATH=$("$VSWHERE" -version "[17.0,18.0)" -latest -requires Microsoft.Component.MSBuild -property installationPath)

if [[ "$PLATFORM" == "ARM64" ]]; then
    VSTEST="$VSPATH\\Common7\\IDE\\Extensions\\TestPlatform\\vstest.console.arm64.exe"
else
    VSTEST="$VSPATH\\Common7\\IDE\\Extensions\\TestPlatform\\vstest.console.exe"
fi

if [ ! -f "$VSTEST" ]; then
    echo "Could not find vstest.console.exe!"
    exit 1
fi

set +e
echo "Testing $PROJECT..."
cd "$PLATFORM/Debug"
"$VSTEST" /Diag:test_$PROJECT.log "$PROJECT.dll"
VSTEST_EXIT_CODE=$?

if [ $VSTEST_EXIT_CODE -ne 0 ]; then
    echo "Test failed!"
    exit $VSTEST_EXIT_CODE
fi

echo "Test succeeded!"
exit 0
