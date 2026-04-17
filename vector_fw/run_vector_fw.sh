#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  ./vector_fw/run_vector_fw.sh <source-file> <qemu|rtl|rtl_vcd>

Examples:
  ./vector_fw/run_vector_fw.sh vec_compare.c qemu
  ./vector_fw/run_vector_fw.sh vec_compare.c rtl
  ./vector_fw/run_vector_fw.sh vec_compare.c rtl_vcd
  RESULT_START_SYMBOL=vdata_start RESULT_END_SYMBOL=vdata_end \
    ./vector_fw/run_vector_fw.sh test_vec.S rtl

Environment variables:
  QEMU_TIMEOUT        QEMU timeout in seconds (default: 3)
  RESULT_SYMBOL       Single dump symbol for RTL (default: c)
  RESULT_START_SYMBOL Start symbol for RTL dump range
  RESULT_END_SYMBOL   End symbol for RTL dump range
  TRACE_VCD_PATH      VCD output path for rtl_vcd mode
EOF
}

if [[ $# -ne 2 ]]; then
    usage
    exit 1
fi

SOURCE_ARG=$1
MODE=$2

case "$MODE" in
    qemu|rtl|rtl_vcd) ;;
    *)
        echo "Unsupported mode: $MODE" >&2
        usage
        exit 1
        ;;
esac

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VICUNA_DIR="$ROOT_DIR/vicuna"
VECTOR_FW_DIR="$ROOT_DIR/vector_fw"
QEMU_RUNTIME_DIR="$VECTOR_FW_DIR/runtime/qemu"
RTL_RUNTIME_DIR="$VECTOR_FW_DIR/runtime/rtl"
SRC_DIR="$VECTOR_FW_DIR/src"
EXAMPLES_DIR="$VECTOR_FW_DIR/examples"
LIB_DIR="$VICUNA_DIR/sw/lib"

if [[ -f "$SOURCE_ARG" ]]; then
    SOURCE_PATH=$(readlink -f "$SOURCE_ARG")
elif [[ -f "$SRC_DIR/$SOURCE_ARG" ]]; then
    SOURCE_PATH=$(readlink -f "$SRC_DIR/$SOURCE_ARG")
elif [[ -f "$EXAMPLES_DIR/$SOURCE_ARG" ]]; then
    SOURCE_PATH=$(readlink -f "$EXAMPLES_DIR/$SOURCE_ARG")
else
    echo "Source file not found: $SOURCE_ARG" >&2
    exit 1
fi

case "$SOURCE_PATH" in
    *.c|*.S|*.s) ;;
    *)
        echo "Only .c, .S, and .s sources are supported: $SOURCE_PATH" >&2
        exit 1
        ;;
esac

detect_riscv_prefix() {
    if command -v riscv32-unknown-elf-gcc >/dev/null 2>&1; then
        echo "riscv32-unknown-elf"
    elif command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
        echo "riscv64-unknown-elf"
    else
        echo ""
    fi
}

RISCV_PREFIX=$(detect_riscv_prefix)
if [[ -z "$RISCV_PREFIX" ]]; then
    echo "Could not find a RISC-V GCC toolchain." >&2
    exit 1
fi

CC="${RISCV_PREFIX}-gcc"
OBJCOPY="${RISCV_PREFIX}-objcopy"
NM="${RISCV_PREFIX}-nm"

if ! command -v srec_cat >/dev/null 2>&1; then
    echo "srec_cat is required but not installed." >&2
    exit 1
fi

SRC_BASENAME=$(basename "$SOURCE_PATH")
SRC_STEM=${SRC_BASENAME%.*}

BUILD_ROOT="$VECTOR_FW_DIR/build_qemu"
RUNTIME_DIR="$QEMU_RUNTIME_DIR"
LINKER_SCRIPT="$QEMU_RUNTIME_DIR/link.ld"
STARTUP_SRC="$QEMU_RUNTIME_DIR/crt0.S"
if [[ "$MODE" == "rtl" || "$MODE" == "rtl_vcd" ]]; then
    BUILD_ROOT="$VECTOR_FW_DIR/build_rtl"
    RUNTIME_DIR="$RTL_RUNTIME_DIR"
    LINKER_SCRIPT="$RTL_RUNTIME_DIR/link.ld"
    STARTUP_SRC="$RTL_RUNTIME_DIR/crt0.S"
fi

BUILD_DIR="$BUILD_ROOT/$SRC_STEM"
mkdir -p "$BUILD_DIR"

WRAPPER_EXT=${SOURCE_PATH##*.}
WRAPPER_SRC="$BUILD_DIR/${SRC_STEM}_wrapper.${WRAPPER_EXT}"
STARTUP_OBJ="$BUILD_DIR/crt0.o"
UART_OBJ="$BUILD_DIR/uart.o"
MAIN_OBJ="$BUILD_DIR/${SRC_STEM}.o"
ELF="$BUILD_DIR/${SRC_STEM}.elf"
BIN="$BUILD_DIR/${SRC_STEM}.bin"
VMEM="$BUILD_DIR/${SRC_STEM}.vmem"
QEMU_OUT="$BUILD_DIR/qemu_output.txt"
QEMU_NORM="$BUILD_DIR/qemu_output.norm.txt"
RTL_DUMP="$BUILD_DIR/rtl_dump.txt"
RTL_NORM="$BUILD_DIR/rtl_dump.norm.txt"
PROGS_FILE="$BUILD_DIR/progs.txt"
TRACE_VCD_PATH="${TRACE_VCD_PATH:-$BUILD_DIR/${SRC_STEM}.vcd}"

RISCV_FLAGS=(
    -march=rv32imv
    -mabi=ilp32
    -static
    -mcmodel=medany
    -fvisibility=hidden
    -nostdlib
    -nostartfiles
    -Wall
)

generate_wrapper_source() {
    case "$SOURCE_PATH" in
        *.c)
            if [[ "$MODE" == "qemu" ]]; then
                printf '#define VEC_COMPARE_QEMU 1\n#include "%s"\n' "$SOURCE_PATH" > "$WRAPPER_SRC"
            else
                printf '#include "%s"\n' "$SOURCE_PATH" > "$WRAPPER_SRC"
            fi
            ;;
        *.S|*.s)
            cp "$SOURCE_PATH" "$WRAPPER_SRC"
            ;;
    esac
}

build_program() {
    generate_wrapper_source

    "$CC" "${RISCV_FLAGS[@]}" -c -o "$STARTUP_OBJ" "$STARTUP_SRC"
    "$CC" "${RISCV_FLAGS[@]}" -O2 -I"$LIB_DIR" -c -o "$MAIN_OBJ" "$WRAPPER_SRC"
    "$CC" "${RISCV_FLAGS[@]}" -O2 -I"$LIB_DIR" -c -o "$UART_OBJ" "$LIB_DIR/uart.c"
    "$CC" "${RISCV_FLAGS[@]}" -T "$LINKER_SCRIPT" "$STARTUP_OBJ" "$MAIN_OBJ" "$UART_OBJ" -o "$ELF"
    "$OBJCOPY" -O binary "$ELF" "$BIN"
    srec_cat "$BIN" -binary -offset 0x0000 -byte-swap 4 -o "$VMEM" -vmem
    rm -f "$BIN"
}

symbol_range_from_named_symbol() {
    local symbol=$1
    "$NM" -S "$ELF" | awk -v sym="$symbol" '$4 == sym { print $1, $2; exit }'
}

symbol_addr() {
    local symbol=$1
    "$NM" "$ELF" | awk -v sym="$symbol" '$3 == sym { print $1; exit }'
}

prepare_rtl_progs() {
    local symbol_name="${RESULT_SYMBOL:-c}"
    local symbol_info
    local start_hex
    local size_hex
    local start_dec
    local end_dec
    local start_sym="${RESULT_START_SYMBOL:-}"
    local end_sym="${RESULT_END_SYMBOL:-}"

    symbol_info=$(symbol_range_from_named_symbol "$symbol_name" || true)
    if [[ -n "$symbol_info" ]]; then
        start_hex=$(awk '{print $1}' <<<"$symbol_info")
        size_hex=$(awk '{print $2}' <<<"$symbol_info")
        if [[ -n "$size_hex" && "$size_hex" != "0" ]]; then
            start_dec=$((16#$start_hex))
            end_dec=$((start_dec + 16#$size_hex))
            printf "%s /dev/null 0 0 %s 0x%x 0x%x\n" \
                "$(readlink -f "$VMEM")" \
                "$(readlink -f "$RTL_DUMP")" \
                "$start_dec" \
                "$end_dec" > "$PROGS_FILE"
            return
        fi
    fi

    if [[ -z "$start_sym" || -z "$end_sym" ]]; then
        if [[ -n "$(symbol_addr vdata_start || true)" && -n "$(symbol_addr vdata_end || true)" ]]; then
            start_sym="vdata_start"
            end_sym="vdata_end"
        fi
    fi

    if [[ -n "$start_sym" && -n "$end_sym" ]]; then
        start_hex=$(symbol_addr "$start_sym")
        end_hex=$(symbol_addr "$end_sym")
        if [[ -n "$start_hex" && -n "$end_hex" ]]; then
            start_dec=$((16#$start_hex))
            end_dec=$((16#$end_hex))
            printf "%s /dev/null 0 0 %s 0x%x 0x%x\n" \
                "$(readlink -f "$VMEM")" \
                "$(readlink -f "$RTL_DUMP")" \
                "$start_dec" \
                "$end_dec" > "$PROGS_FILE"
            return
        fi
    fi

    echo "Failed to determine RTL dump range from $ELF." >&2
    echo "Set RESULT_SYMBOL, or RESULT_START_SYMBOL and RESULT_END_SYMBOL." >&2
    exit 1
}

run_qemu() {
    local qemu_status
    local qemu_timeout="${QEMU_TIMEOUT:-3}"

    rm -f "$QEMU_OUT" "$QEMU_NORM"

    set +e
    timeout "$qemu_timeout" qemu-system-riscv32 \
        -machine virt \
        -cpu rv32,v=true \
        -bios none \
        -kernel "$ELF" \
        -display none \
        -serial "file:$QEMU_OUT" \
        -monitor none >/dev/null 2>&1
    qemu_status=$?
    set -e

    if [[ $qemu_status -ne 0 && $qemu_status -ne 124 ]]; then
        cat "$QEMU_OUT" >&2
        exit $qemu_status
    fi

    if [[ -s "$QEMU_OUT" ]]; then
        grep -E '^[0-9a-fA-F]{8}$' "$QEMU_OUT" | tr 'A-F' 'a-f' > "$QEMU_NORM" || true
        if [[ -s "$QEMU_NORM" ]]; then
            cat "$QEMU_NORM"
        else
            cat "$QEMU_OUT"
        fi
    fi

    echo
    echo "ELF: $ELF"
    echo "VMEM: $VMEM"
    echo "QEMU output: $QEMU_OUT"
}

run_rtl() {
    rm -f "$RTL_DUMP" "$RTL_NORM" "$PROGS_FILE"
    prepare_rtl_progs

    if [[ "$MODE" == "rtl_vcd" ]]; then
        mkdir -p "$(dirname "$TRACE_VCD_PATH")"
        rm -f "$TRACE_VCD_PATH"
        make -C "$VICUNA_DIR/sim" verilator \
            PROG_PATHS="$(readlink -f "$PROGS_FILE")" \
            TRACE_VCD="$(readlink -m "$TRACE_VCD_PATH")"
    else
        make -C "$VICUNA_DIR/sim" verilator \
            PROG_PATHS="$(readlink -f "$PROGS_FILE")"
    fi

    if [[ -s "$RTL_DUMP" ]]; then
        grep -E '^[0-9a-fA-F]{8}$' "$RTL_DUMP" | tr 'A-F' 'a-f' > "$RTL_NORM" || true
        if [[ -s "$RTL_NORM" ]]; then
            cat "$RTL_NORM"
        else
            cat "$RTL_DUMP"
        fi
    fi

    echo
    echo "ELF: $ELF"
    echo "VMEM: $VMEM"
    echo "RTL dump: $RTL_DUMP"
    echo "RTL progs: $PROGS_FILE"
    if [[ "$MODE" == "rtl_vcd" ]]; then
        echo "VCD: $(readlink -m "$TRACE_VCD_PATH")"
    fi
}

build_program

case "$MODE" in
    qemu)
        run_qemu
        ;;
    rtl|rtl_vcd)
        run_rtl
        ;;
esac
