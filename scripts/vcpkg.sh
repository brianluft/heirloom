#!/bin/bash
# Runs vcpkg with the given arguments.
set -euo pipefail

# Locate vswhere
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -f "$VSWHERE" ]; then
    echo "Could not find vswhere.exe!"
    exit 1
fi

# Use vswhere to locate the VS installation path, e.g. "C:\Program Files\Microsoft Visual Studio\2022\Community"
VSPATH=$("$VSWHERE" -version "[17.0,18.0)" -latest -requires Microsoft.Component.MSBuild -property installationPath)
if [ -z "$VSPATH" ]; then
    echo "Could not find VS installation path!"
    exit 1
fi

# Locate vcpkg
VCPKG="$VSPATH\VC\vcpkg\vcpkg.exe"
if [ ! -f "$VCPKG" ]; then
    echo "Could not find vcpkg.exe at $VCPKG!"
    exit 1
fi

# Finally, run vcpkg with the given arguments
"$VCPKG" "$@"
