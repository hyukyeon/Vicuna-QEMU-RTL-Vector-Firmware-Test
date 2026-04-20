#!/usr/bin/env bash
set -eu
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="vec_vsmul_red.S"

echo "Building and running QEMU for vsmul/reduction tests..."
"$ROOT_DIR/vector_fw/run_vector_fw.sh" "$SRC" qemu

echo "Building and running RTL for vsmul/reduction tests..."
"$ROOT_DIR/vector_fw/run_vector_fw.sh" "$SRC" rtl

BUILD_QEMU="$ROOT_DIR/vector_fw/build_qemu/${SRC%.*}"
BUILD_RTL="$ROOT_DIR/vector_fw/build_rtl/${SRC%.*}"
QEMU_NORM="$BUILD_QEMU/qemu_output.norm.txt"
RTL_NORM="$BUILD_RTL/rtl_dump.norm.txt"

if [[ -s "$QEMU_NORM" ]]; then
    echo "--- QEMU ---"
    sed -n '1,200p' "$QEMU_NORM" || true
fi
if [[ -s "$RTL_NORM" ]]; then
    echo "--- RTL ---"
    sed -n '1,200p' "$RTL_NORM" || true
fi

if diff -u "$QEMU_NORM" "$RTL_NORM" >/dev/null ; then
    echo "PASS: QEMU and RTL outputs match"
    exit 0
else
    echo "FAIL: outputs differ"
    diff -u "$QEMU_NORM" "$RTL_NORM" || true
    exit 3
fi
