# vector_fw

이 디렉터리는 Vicuna 원본과 분리한 펌웨어 / runtime / wrapper만 담는다.

핵심 파일:

- `src/vec_compare.c`: 공통 C 벡터 펌웨어
- `examples/`: 추가 실험용 예제
- `runtime/qemu/`: QEMU용 startup / linker
- `runtime/rtl/`: RTL용 startup / linker
- `run_vector_fw.sh`: 단일 실행 wrapper

실행은 저장소 루트에서 한다.

```sh
./vector_fw/run_vector_fw.sh vec_compare.c qemu
./vector_fw/run_vector_fw.sh vec_compare.c rtl
./vector_fw/run_vector_fw.sh vec_compare.c rtl_vcd
```

산출물은 자동으로 아래에 모인다.

- `vector_fw/build_qemu/<name>/`
- `vector_fw/build_rtl/<name>/`

`rtl_vcd`는 기본적으로 `vector_fw/build_rtl/<name>/<name>.vcd`를 만든다.
