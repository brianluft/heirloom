#!/bin/bash
set -eo pipefail

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -f "$VSWHERE" ]; then
    echo "Could not find vswhere.exe!"
    exit 1
fi

VSPATH=$("$VSWHERE" -all -property installationPath)
CLANG_FORMAT="$VSPATH\\VC\\Tools\\Llvm\\bin\\clang-format.exe"
if [ ! -f "$CLANG_FORMAT" ]; then
    echo "Could not find clang-format.exe!"
    echo "Use Visual Studio Installer to install the clang feature."
    exit 1
fi

# Change to root directory.
cd "$( dirname "${BASH_SOURCE[0]}" )"
cd ..

# Check for non-UTF-8 text files and convert them to UTF-8.
powershell.exe -ExecutionPolicy Bypass -File "scripts/Convert-ToUtf8.ps1" 

# Change to src directory.
cd src

# Format a project's source files
format_project() {
    local project_name=$1
    echo "Formatting $project_name..."
    find $project_name -type f \( -iname \*.h -o -iname \*.cpp \) | xargs "$CLANG_FORMAT" -i
}

format_project "winfile"
format_project "libwinfile"
format_project "libwinfile_tests"
format_project "libprogman"
format_project "libprogman_tests"
format_project "progman"
