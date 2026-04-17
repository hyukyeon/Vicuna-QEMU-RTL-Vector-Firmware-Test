# Vicuna QEMU-RTL Vector Firmware Test

`vector_fw/`는 **Vicuna 원본 트리와 분리된 벡터 펌웨어 테스트 프로젝트**다.  
이 프로젝트는 같은 단일 펌웨어 소스를:

- **QEMU (`qemu-system-riscv32`)**
- **Vicuna RTL 시뮬레이션 (Verilator)**

에서 각각 실행해 결과를 확인할 수 있게 해준다.

이 저장소에는 Vicuna 원본을 포함하지 않는다. 대신, 로컬에 `vicuna/` 디렉터리로 upstream Vicuna를 clone해 두는 구성을 전제로 한다.


## 디렉터리 구성

- `vector_fw/src/`: 공통 C 펌웨어
- `vector_fw/examples/`: 실험용 예제 소스
- `vector_fw/runtime/qemu/`: QEMU용 startup / linker script
- `vector_fw/runtime/rtl/`: RTL용 startup / linker script
- `vector_fw/build_qemu/`: QEMU 빌드/실행 산출물
- `vector_fw/build_rtl/`: RTL 빌드/실행 산출물
- `vector_fw/run_vector_fw.sh`: 단일 wrapper 실행 스크립트


## 빈 디렉터리에서 시작하는 설치 / 준비 방법

아래 예시는 Ubuntu 계열 환경 기준이다.

### 1. 필수 패키지 설치

```sh
sudo apt update
sudo apt install -y \
  git gh make \
  verilator \
  qemu-system-misc \
  gcc-riscv64-unknown-elf \
  binutils-riscv64-unknown-elf \
  srecord
```

선택 사항:

```sh
sudo apt install -y gtkwave
```

설명:

- `qemu-system-riscv32`는 보통 `qemu-system-misc` 패키지에 포함된다.
- 이 프로젝트는 현재 `riscv64-unknown-elf-gcc`로 **RV32** 바이너리를 빌드한다.


### 2. 작업 디렉터리 생성

```sh
mkdir -p ~/vicuna-fw-test
cd ~/vicuna-fw-test
```


### 3. 이 저장소 clone

```sh
git clone https://github.com/hyukyeon/Vicuna-QEMU-RTL-Vector-Firmware-Test.git
cd Vicuna-QEMU-RTL-Vector-Firmware-Test
```


### 4. Vicuna 원본 및 submodule clone

이 프로젝트의 wrapper는 루트 기준 `./vicuna` 경로를 기대한다.

```sh
git clone https://github.com/vproc/vicuna.git vicuna
cd vicuna
git submodule update --init --recursive
cd ..
```


## 실행 방법

루트에서 아래 명령으로 실행한다.

```sh
./vector_fw/run_vector_fw.sh <source-file> <qemu|rtl|rtl_vcd>
```

예:

```sh
./vector_fw/run_vector_fw.sh vec_compare.c qemu
./vector_fw/run_vector_fw.sh vec_compare.c rtl
./vector_fw/run_vector_fw.sh vec_compare.c rtl_vcd
```

상대 경로 소스는 우선 현재 디렉터리, 그 다음 `vector_fw/src/`, `vector_fw/examples/`에서 찾는다.


## QEMU 실행

예:

```sh
./vector_fw/run_vector_fw.sh vec_compare.c qemu
```

산출물:

- `vector_fw/build_qemu/<source-stem>/<source-stem>.elf`
- `vector_fw/build_qemu/<source-stem>/<source-stem>.vmem`
- `vector_fw/build_qemu/<source-stem>/qemu_output.txt`
- `vector_fw/build_qemu/<source-stem>/qemu_output.norm.txt`

현재 `vec_compare.c`의 정상 출력 예:

```text
0000000b
00000016
00000021
0000002c
00000037
00000042
0000004d
00000058
```


## RTL 시뮬레이션 실행

예:

```sh
./vector_fw/run_vector_fw.sh vec_compare.c rtl
```

산출물:

- `vector_fw/build_rtl/<source-stem>/<source-stem>.elf`
- `vector_fw/build_rtl/<source-stem>/<source-stem>.vmem`
- `vector_fw/build_rtl/<source-stem>/rtl_dump.txt`
- `vector_fw/build_rtl/<source-stem>/rtl_dump.norm.txt`
- `vector_fw/build_rtl/<source-stem>/progs.txt`


## VCD 포함 RTL 실행

예:

```sh
./vector_fw/run_vector_fw.sh vec_compare.c rtl_vcd
```

기본 VCD 경로:

```text
vector_fw/build_rtl/vec_compare/vec_compare.vcd
```

VCD 경로를 바꾸려면:

```sh
TRACE_VCD_PATH=/desired/path/out.vcd \
  ./vector_fw/run_vector_fw.sh vec_compare.c rtl_vcd
```

생성된 VCD는 다음처럼 볼 수 있다.

```sh
gtkwave vector_fw/build_rtl/vec_compare/vec_compare.vcd
```


## 어셈블리 예제 실행 시 RTL 덤프 범위 지정

일부 어셈블리 예제는 결과 배열 심볼 `c`가 없으므로 덤프 범위를 명시해 줘야 한다.

예:

```sh
RESULT_START_SYMBOL=vdata_start RESULT_END_SYMBOL=vdata_end \
  ./vector_fw/run_vector_fw.sh test_vec.S rtl
```

RTL wrapper는 기본적으로 아래 순서로 덤프 범위를 찾는다.

1. `RESULT_SYMBOL` 환경변수로 지정한 단일 심볼 (`기본값: c`)
2. `RESULT_START_SYMBOL` / `RESULT_END_SYMBOL`
3. `vdata_start` / `vdata_end`


## 현재 확인된 예제

현재 `vec_compare.c`에 대해:

- QEMU 실행 정상
- RTL 실행 정상
- RTL VCD 생성 정상

이고, 동일한 결과 벡터가 출력되는 것을 확인했다.
