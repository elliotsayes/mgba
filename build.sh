#!/bin/sh
set -e
cd "$(dirname "$0")"
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
