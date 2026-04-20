#!/usr/bin/env bash
set -eu
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="vec_alu_more.c"

echo "Building and running QEMU for more ALU tests..."
"$ROOT_DIR/vector_fw/run_vector_fw.sh" "$SRC" qemu

echo "Building and running RTL for more ALU tests..."
"$ROOT_DIR/vector_fw/run_vector_fw.sh" "$SRC" rtl

BUILD_QEMU="$ROOT_DIR/vector_fw/build_qemu/${SRC%.*}"
BUILD_RTL="$ROOT_DIR/vector_fw/build_rtl/${SRC%.*}"
QEMU_NORM="$BUILD_QEMU/qemu_output.norm.txt"
RTL_NORM="$BUILD_RTL/rtl_dump.norm.txt"

if [[ ! -s "$QEMU_NORM" ]]; then
    echo "QEMU normalized output missing or empty: $QEMU_NORM" >&2
    exit 2
fi
if [[ ! -s "$RTL_NORM" ]]; then
    echo "RTL normalized output missing or empty: $RTL_NORM" >&2
    exit 2
fi

echo "--- QEMU ---"
sed -n '1,200p' "$QEMU_NORM" || true
echo "--- RTL ---"
sed -n '1,200p' "$RTL_NORM" || true

if diff -u "$QEMU_NORM" "$RTL_NORM" >/dev/null ; then
    echo "PASS: QEMU and RTL outputs match"
    exit 0
else
    echo "FAIL: outputs differ"
    diff -u "$QEMU_NORM" "$RTL_NORM" || true
    exit 3
fi
