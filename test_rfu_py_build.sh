#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build-py}"
JOBS="${JOBS:-8}"
PYTHON="${PYTHON:-$PWD/.venv/bin/python}"
TEST_PATH="${TEST_PATH:-src/platform/python/tests/mgba/test_gba_rfu.py}"
MODE="${MODE:-py}"
NEEDS_CONFIGURE=0

case "${1:-}" in
  --full)
    MODE="full"
    ;;
  --help|-h)
    cat <<'EOF'
Usage: ./test_rfu_py_build.sh [--full]

Defaults:
  - Fast Python RFU loop (rebuild C core, run Python test directly)

Options:
  --full    Also build and run the C cmocka RFU test (gba-rfu)

Environment:
  BUILD_DIR=build-py
  JOBS=8
  PYTHON=.venv/bin/python
  TEST_PATH=src/platform/python/tests/mgba/test_gba_rfu.py
  FORCE_PY_BUILD=1    Rebuild mgba-py target before running tests
EOF
    exit 0
    ;;
esac

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  NEEDS_CONFIGURE=1
fi

if [[ "${FORCE_CONFIGURE:-0}" == "1" ]]; then
  NEEDS_CONFIGURE=1
fi

if [[ "${NEEDS_CONFIGURE}" == "0" ]]; then
  for KV in \
    "BUILD_PYTHON:BOOL=ON" \
    "BUILD_TEST:BOOL=ON" \
    "BUILD_SUITE:BOOL=ON" \
    "BUILD_LTO:BOOL=OFF" \
    "BUILD_QT:BOOL=OFF" \
    "BUILD_SDL:BOOL=OFF" \
    "USE_FFMPEG:BOOL=OFF" \
    "USE_DISCORD_RPC:BOOL=OFF" \
    "ENABLE_SCRIPTING:BOOL=OFF"
  do
    if ! grep -q "^${KV}$" "${BUILD_DIR}/CMakeCache.txt"; then
      NEEDS_CONFIGURE=1
      break
    fi
  done
fi

if [[ "${NEEDS_CONFIGURE}" == "1" ]]; then
  cmake -S . -B "${BUILD_DIR}" \
    -DBUILD_PYTHON=ON -DBUILD_TEST=ON -DBUILD_SUITE=ON \
    -DPYTHON_EXECUTABLE="${PYTHON}" \
    -DBUILD_LTO=OFF \
    -DBUILD_QT=OFF -DBUILD_SDL=OFF \
    -DUSE_FFMPEG=OFF -DUSE_DISCORD_RPC=OFF \
    -DENABLE_SCRIPTING=OFF >/dev/null
fi

cmake --build "${BUILD_DIR}" --target mgba -j"${JOBS}"

if [[ "${FORCE_PY_BUILD:-0}" == "1" ]] || ! find "${BUILD_DIR}/python" -maxdepth 4 -type f -name '_pylib*.so' | grep -q .; then
  cmake --build "${BUILD_DIR}" --target mgba-py -j"${JOBS}"
fi

PYLIB="$(find "${BUILD_DIR}/python" -maxdepth 1 -type d -name 'lib.*' | head -n1)"
if [[ -z "${PYLIB}" ]]; then
  echo "No built Python package found in ${BUILD_DIR}/python/lib.*; run with FORCE_PY_BUILD=1" >&2
  exit 1
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  LIB_PATH_VAR="DYLD_LIBRARY_PATH"
else
  LIB_PATH_VAR="LD_LIBRARY_PATH"
fi

env "${LIB_PATH_VAR}=$PWD/${BUILD_DIR}" \
  PYTHONPATH="${PYLIB}" \
  PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 \
  "${PYTHON}" -m pytest -q --import-mode=importlib "${TEST_PATH}" -x

if [[ "${MODE}" == "full" ]]; then
  cmake --build "${BUILD_DIR}" --target test-gba-rfu -j"${JOBS}"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure -R "^gba-rfu$"
fi
