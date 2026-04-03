#!/bin/bash
# Optimised build script for the slot simulation.
#
# Key flags:
#   -O3            full optimisation (loop unrolling, vectorisation, inlining)
#   -march=native  use all SIMD/CPU extensions available on this machine
#   -flto          link-time optimisation (cross-translation-unit inlining)
#   -funroll-loops  aggressively unroll small loops (helps the spin inner loop)
#   -ffast-math    relax IEEE754 rules for faster FP maths (safe for RTP calcs)
#   -pthread       POSIX threads

set -e

CXX=${CXX:-g++}
CXXFLAGS="-O3 -march=native -flto -funroll-loops -ffast-math -std=c++17 -pthread"
SRC="main.cpp"   # RandomLogGenerator.cpp bodies are already inlined in the .h
OUT="sim"

echo "Building with: $CXX $CXXFLAGS"
$CXX $CXXFLAGS $SRC -o $OUT
echo "Done — run with: ./$OUT"
