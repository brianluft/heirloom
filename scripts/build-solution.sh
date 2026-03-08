#!/bin/bash
# Required variable: SOLUTION
set -euo pipefail

# Check SOLUTION
if [ -z "${SOLUTION:-}" ]; then
    echo "SOLUTION is not set!"
    exit 1
fi

echo "Building $SOLUTION..."

# Change to the src directory.
cd "$( dirname "${BASH_SOURCE[0]}" )"
cd ../src

# Use vswhere to locate msbuild.exe
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -f "$VSWHERE" ]; then
    echo "Could not find vswhere.exe!"
    exit 1
fi

MSBUILD=$("$VSWHERE" -version "[17.0,18.0)" -latest -requires Microsoft.Component.MSBuild -find "MSBuild/**/Bin/MSBuild.exe" | head -n 1)
if [ ! -f "$MSBUILD" ]; then
    echo "Could not find msbuild.exe!"
    exit 1
fi

PLATFORM=$(../scripts/get-native-arch.sh)

# Build the solution
set +e
"$MSBUILD" "$SOLUTION" --p:Configuration=Debug --p:Platform=$PLATFORM --verbosity:minimal --nologo 2>&1
MSBUILD_EXIT_CODE=$?

if [ $MSBUILD_EXIT_CODE -ne 0 ]; then
    echo "Build failed!"
    exit $MSBUILD_EXIT_CODE
fi

echo "Build succeeded!"
exit 0
