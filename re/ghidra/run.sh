#!/bin/sh
# Import BL2.OVL into a headless Ghidra project and dump decompiled C to bl2_decomp.c.
# Needs GHIDRA (default ~/tools/ghidra_*) and, on aarch64, a natively built decompiler.
set -e
cd "$(dirname "$0")"
GHIDRA=${GHIDRA:-$(ls -d ~/tools/ghidra_*_PUBLIC | tail -1)}
mkdir -p proj
"$GHIDRA/support/analyzeHeadless" "$PWD/proj" bl2 -import "$PWD/../../original/BL2.OVL" -overwrite \
    -scriptPath "$PWD" ${PRESCRIPT:+-preScript $PRESCRIPT} -postScript DumpAll.java "$PWD/bl2_decomp.c" > headless.log 2>&1
grep -c '=====' bl2_decomp.c
