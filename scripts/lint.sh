#!/usr/bin/env bash
mkdir -p build/lint
pushd build/lint 2> /dev/null
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ../..
run-clang-tidy
