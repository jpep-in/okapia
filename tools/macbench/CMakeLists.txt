# MacBench: a self-running benchmark for a 68k Macintosh, built with Retro68.
# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: MIT
# scripts/build-macbench.sh runs this; by hand:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
cmake_minimum_required(VERSION 3.16)
project(MacBench C)
add_application(MacBench macbench.c fpu.c macbench.r)
set_target_properties(MacBench PROPERTIES COMPILE_OPTIONS "-O2;-ffunction-sections")
# The floating-point tests use the coprocessor's own instructions; the rest
# stays 68000 code, and macbench.c calls them only when there is an FPU.
set_source_files_properties(fpu.c PROPERTIES COMPILE_OPTIONS "-m68020;-m68881")
set_target_properties(MacBench PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-single")
