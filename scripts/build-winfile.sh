#!/bin/bash
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"
SOLUTION=winfile.sln ./build-solution.sh
PROJECT=libwinfile_tests ./test-project.sh
