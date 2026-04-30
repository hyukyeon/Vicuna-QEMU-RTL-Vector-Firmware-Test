# Vicuna Vector ISA

Vicuna RTL(`vicuna/rtl/vproc_decoder.sv`, `vicuna/rtl/vproc_pkg.sv`) 기준으로 **실제로 디코드되는 vector instruction**을 정리한 문서다.  
아래의 `prototype`은 실제 C 헤더가 아니라 **의미를 설명하기 위한 pseudo prototype**이다.

## 1. 지원 데이터 타입 / vtype 제약

- **SEW**: `e8`, `e16`, `e32`
- **LMUL**: `mf8`, `mf4`, `mf2`, `m1`, `m2`, `m4`, `m8`
- **데이터 타입**: 정수형만 지원
  - signed/unsigned 8-bit
  - signed/unsigned 16-bit
  - signed/unsigned 32-bit
  - mask vector
- **미지원**: floating-point vector, complex type, 64-bit element, divide/remainder 계열

**지원 범위 요약**:

Vicuna는 대체로 다음 RVV subset을 구현한다.

- **정수 vector ALU** (add/sub/min/max/logic/shift/widening)
- **정수 load/store** (unit-stride, strided, indexed, segment, whole-register, mask, fault-only-first)
- **mask compare / boolean**
- **fixed-point saturating / rounding**
- **multiply / MAC / widening MAC**
- **slide / gather / compress**
- **reduction / element utility**

미지원 범위:
- floating-point vector
- vector divide / remainder
- 64-bit element 연산
- complex number 전용 연산

## 2. 표기법

```c
// pseudo types
typedef uint32_t xreg;
typedef int32_t  simm;
typedef uint32_t uimm;
typedef uintptr_t addr;

template<typename T> using vec = /* vector register group */;
using mask = /* v0-based mask */;
```

- `vv`: vector-vector
- `vx`: vector-scalar
- `vi`: vector-immediate
- `vm`: mask 사용
- `vd`: destination vector
- `vs2`, `vs1`: source vector
- `rs1`: scalar integer register

**검증(검증 열) 표기**:
- `✓` : QEMU == RTL (일치)
- `❌` : RTL 버그 (QEMU와 불일치, 섹션 12 버그표 참조)
- `-` : 미검증

## 3. Configuration / CSR

테스트: `vector_fw/unit_test/test_ch3_config.c` — CH3 QEMU/RTL 비교 결과: **4개 불일치** (vsetvl e32mf2, vxsat, vxrm, vcsr)

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vsetvl` | `xreg vsetvl(xreg avl, xreg vtype)` | `rd`에 새 VL 반환 | ✓ |
| `vsetvli` | `xreg vsetvli(xreg avl, uimm vtypei)` | immediate vtype | ✓ |
| `vsetivli` | `xreg vsetivli(uimm avl, uimm vtypei)` | immediate AVL + vtype | ✓ |
| `vsetvl` / `vsetvli` with `e32, mf2` | — | ❌ RTL 버그: vl=0 반환 (spec은 VLMAX=2 요구). e16,mf2 / e8,mf2는 정상 | ❌ |
| `csrrw/csrrs/csrrc` on `vstart` | `xreg csr_vstart(xreg value)` | read/write/set/clear 지원 | - |
| `csrrw/csrrs/csrrc` on `vxsat` | `xreg csr_vxsat(xreg value)` | ❌ RTL 버그: 포화 연산 후에도 vxsat 비트 미갱신, csrr 항상 0 반환 | ❌ |
| `csrrw/csrrs/csrrc` on `vxrm` | `xreg csr_vxrm(xreg value)` | ❌ RTL 버그: csrw로 쓴 값이 csrr로 읽히지 않음 (항상 0) | ❌ |
| `csrrw/csrrs/csrrc` on `vcsr` | `xreg csr_vcsr(xreg value)` | ❌ RTL 버그: vxsat/vxrm 반영 안 됨, 항상 0 | ❌ |
| `csrrs/csrrc` on `vl` | `xreg read_vl()` | read-only | ✓ |
| `csrrs/csrrc` on `vtype` | `xreg read_vtype()` | read-only | - |
| `csrrs/csrrc` on `vlenb` | `xreg read_vlenb()` | read-only, VLEN=128 → 16 | ✓ |

참고:
- `vsetvl x0, x0, rs2` 형태의 **keep VL** 패턴도 RTL에서 처리한다.
- `vtype`는 `SEW=8/16/32`, `LMUL=mf8..m8`만 유효하다. 단, `e32,mf2` 조합은 RTL에서 vl=0 반환.

## 4. Load / Store

테스트: `vector_fw/unit_test/test_ch4_loadstore.c` — CH4 QEMU/RTL 비교 결과: **3개 불일치** (vloxei32/vsoxei32 비제로 인덱스)

### 4.1 기본 load/store

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vle8.v`, `vle16.v`, `vle32.v` | `vec<T> vle(addr base, mask m=all)` | ✓ |
| `vse8.v`, `vse16.v`, `vse32.v` | `void vse(addr base, vec<T> vs3, mask m=all)` | ✓ |
| `vlse8.v`, `vlse16.v`, `vlse32.v` | `vec<T> vlse(addr base, xreg stride, mask m=all)` | ✓ |
| `vsse8.v`, `vsse16.v`, `vsse32.v` | `void vsse(addr base, xreg stride, vec<T> vs3, mask m=all)` | ✓ |

### 4.2 indexed / segment / whole-register / mask load-store

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vloxei8.v`, `vloxei16.v`, `vloxei32.v` | `vec<T> vloxei(addr base, vec<IdxT> index, mask m=all)` | index=0은 ✓; **비제로 byte-offset index: ❌ RTL spec deviation** (§4.3) | ❌ |
| `vsoxei8.v`, `vsoxei16.v`, `vsoxei32.v` | `void vsoxei(addr base, vec<IdxT> index, vec<T> vs3, mask m=all)` | 동일 spec deviation | ❌ |
| indexed encodings (`mop=01/11`) | `vec<T> vlxei(...)/vsxei(...)` | RTL은 ordered/unordered indexed encoding을 모두 수용 | - |
| `vlseg<nf>e8.v`, `vlseg<nf>e16.v`, `vlseg<nf>e32.v` | `tuple<nf, vec<T>> vlseg(addr base, mask m=all)` | `nf=2..8` | ✓ |
| `vsseg<nf>e8.v`, `vsseg<nf>e16.v`, `vsseg<nf>e32.v` | `void vsseg(addr base, tuple<nf, vec<T>> data, mask m=all)` | `nf=2..8` | ✓ |
| `vle8ff.v`, `vle16ff.v`, `vle32ff.v` | `vec<T> vleff(addr base, mask m=all)` | fault-only-first load | ✓ |
| `vl1r.v`, `vl2r.v`, `vl4r.v`, `vl8r.v` | `vec<T> vlNr(addr base)` | whole-register load | ✓ |
| `vs1r.v`, `vs2r.v`, `vs4r.v`, `vs8r.v` | `void vsNr(addr base, vec<T> data)` | whole-register store | ✓ |
| `vlm.v` | `mask vlm(addr base)` | mask load | ✓ |
| `vsm.v` | `void vsm(addr base, mask data)` | **❌ RTL 버그**: BE/wdata 형성 오류 (CH6 테스트에서 ~17라인 불일치) | ❌ |

### 4.3 indexed load/store spec deviation

`vloxei`/`vsoxei`는 RVV spec과 동작이 다르다.

**RVV spec (QEMU)**: index 값이 byte offset
```
effective_address = base + index[i]    // index는 바이트 단위
```

**Vicuna RTL** (`vproc_lsu.sv` line 229): index 값에 element_size를 내부에서 추가로 곱함
```systemverilog
VSEW_32: req_addr_d = xval + {vs2_data[29:0] * (1 + nfields), 2'b0};
// = base + index[i] * eew_bytes       // index를 element index로 취급
```

| EEW | Vicuna 실제 주소 |
|-----|----------------|
| e8  | `base + index[i] * 1` |
| e16 | `base + index[i] * 2` |
| e32 | `base + index[i] * 4` |

따라서 Vicuna RTL에서 vloxei32/vsoxei32를 올바르게 사용하려면 index 값을 **byte offset이 아닌 element index**로 지정해야 한다.

```c
// QEMU (V spec)에 맞는 코드: byte offset
static uint32_t index_data[4] = {0, 4, 8, 12};  // byte offset

// Vicuna RTL에 맞는 코드: element index
static uint32_t index_data[4] = {0, 1, 2, 3};   // element index
```

두 방식이 모두 정확하게 동작하는 단일 테스트를 작성하는 것은 이 spec deviation으로 인해 불가능하다. QEMU 비교용 테스트는 byte offset, RTL 단독 테스트는 element index를 사용해야 한다.

## 5. Integer ALU

테스트: `vector_fw/unit_test/test_ch5_int_alu.c` — CH5 QEMU/RTL 비교 결과: **12개 불일치** (vnsrl, vnsra, vzext.vf2, vsext.vf2)

### 5.1 기본 add/sub/min/max/logic

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vadd.vv`, `vadd.vx`, `vadd.vi` | `vec<T> vadd(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vsub.vv`, `vsub.vx` | `vec<T> vsub(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vrsub.vx`, `vrsub.vi` | `vec<T> vrsub(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vminu.vv`, `vminu.vx` | `vec<T> vminu(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmin.vv`, `vmin.vx` | `vec<T> vmin(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmaxu.vv`, `vmaxu.vx` | `vec<T> vmaxu(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmax.vv`, `vmax.vx` | `vec<T> vmax(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vand.vv`, `vand.vx`, `vand.vi` | `vec<T> vand(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vor.vv`, `vor.vx`, `vor.vi` | `vec<T> vor(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vxor.vv`, `vxor.vx`, `vxor.vi` | `vec<T> vxor(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |

### 5.2 shift / narrow shift

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vsll.vv`, `vsll.vx`, `vsll.vi` | `vec<T> vsll(vec<T> vs2, Arg shamt, mask m=all)` | | ✓ |
| `vsrl.vv`, `vsrl.vx`, `vsrl.vi` | `vec<T> vsrl(vec<T> vs2, Arg shamt, mask m=all)` | | ✓ |
| `vsra.vv`, `vsra.vx`, `vsra.vi` | `vec<T> vsra(vec<T> vs2, Arg shamt, mask m=all)` | | ✓ |
| `vnsrl.wv`, `vnsrl.wx`, `vnsrl.wi` | `vec<NarrowT> vnsrl(vec<WideT> vs2, Arg shamt, mask m=all)` | ❌ RTL 미동작 (아래 참고) | ❌ |
| `vnsra.wv`, `vnsra.wx`, `vnsra.wi` | `vec<NarrowT> vnsra(vec<WideT> vs2, Arg shamt, mask m=all)` | ❌ RTL 미동작 (아래 참고) | ❌ |

> **vnsrl / vnsra RTL 미동작**: OP_NARROWING 경로(`vproc_decoder.sv` line 1577)의 `vl_o` 계산 버그. `vl_o = {vl_i[CFG_VL_W-2:0], 1'b1}`으로 `vl_i=4`이면 `vl_o=9`가 되어 VLMAX 초과. 결과: 짝수 레인에 시프트 미적용 원본값, 홀수 레인에 0.  
> 실제 RTL 출력(e16,mf2 vnsrl.wi >>2, 입력 {0x11,0x22,0x33,0x44}):  
> `{0x0011, 0x0000, 0x0022, 0x0000}` (기대값: `{0x0004, 0x0008, 0x000C, 0x0011}`)

### 5.3 widening / extension

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vwaddu.vv`, `vwaddu.vx` | `vec<WideU> vwaddu(vec<U> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwadd.vv`, `vwadd.vx` | `vec<WideS> vwadd(vec<S> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwsubu.vv`, `vwsubu.vx` | `vec<WideU> vwsubu(vec<U> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwsub.vv`, `vwsub.vx` | `vec<WideS> vwsub(vec<S> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwaddu.wv`, `vwaddu.wx` | `vec<WideU> vwaddu_w(vec<WideU> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwadd.wv`, `vwadd.wx` | `vec<WideS> vwadd_w(vec<WideS> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwsubu.wv`, `vwsubu.wx` | `vec<WideU> vwsubu_w(vec<WideU> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vwsub.wv`, `vwsub.wx` | `vec<WideS> vwsub_w(vec<WideS> vs2, Arg rhs, mask m=all)` | | ✓ |
| `vzext.vf2` | `vec<WideU> vzext_vf2(vec<U> vs2, mask m=all)` | ❌ RTL 미동작: zero 출력 (아래 참고) | ❌ |
| `vsext.vf2` | `vec<WideS> vsext_vf2(vec<S> vs2, mask m=all)` | ❌ RTL 미동작: illegal instruction 예외 (아래 참고) | ❌ |

> **vzext.vf2 / vsext.vf2 RTL 미동작**: 디코더에 엔트리(`{6'b010010, 3'b010}`, `vproc_decoder.sv` line 657)는 존재하지만 아래 두 가지 버그로 인해 정상 동작하지 않는다.
>
> **버그 1 — vzext.vf2 (zero 출력)**  
> OP_WIDENING 경로(`vproc_decoder.sv` lines 1562-1566)는 `VSEW_8→VSEW_16`, `VSEW_16→VSEW_32` 만 처리하고, **`VSEW_32` 케이스가 없어** `vsew_o`가 don't-care 상태가 된다.
>
> **버그 2 — vsext.vf2 (illegal instruction 예외)**  
> OP_WIDENING은 `emul_o`를 m1→m2(EMUL_2)로 증가시킨다. EMUL_2는 짝수 정렬(even-aligned)을 요구하는데, 컴파일러가 홀수 번호 레지스터를 `vd`로 할당하면 `vd_invalid=1` → `op_illegal=1` → `valid_o=0`이 되어 Ibex가 illegal instruction 예외를 발생시킨다.

## 6. Carry / compare / mask-producing instructions

테스트: `vector_fw/unit_test/test_ch6_mask.c` — CH6 QEMU/RTL 비교 결과: **~17개 불일치** (vsm 마스크 저장 버그로 인한 cascade)

### 6.1 carry / borrow

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vadc.vv`, `vadc.vx`, `vadc.vi` | `vec<T> vadc(vec<T> vs2, Arg rhs, mask carry_in)` | ✓ |
| `vsbc.vv`, `vsbc.vx`, `vsbc.vi` | `vec<T> vsbc(vec<T> vs2, Arg rhs, mask borrow_in)` | ✓ |
| `vmadc.vv`, `vmadc.vx`, `vmadc.vi` | `mask vmadc(vec<T> vs2, Arg rhs, mask carry_in=0)` | ❌ (vsm 저장 버그) |
| `vmsbc.vv`, `vmsbc.vx`, `vmsbc.vi` | `mask vmsbc(vec<T> vs2, Arg rhs, mask borrow_in=0)` | ❌ (vsm 저장 버그) |

### 6.2 compare

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vmseq.vv`, `vmseq.vx`, `vmseq.vi` | `mask vmseq(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsne.vv`, `vmsne.vx`, `vmsne.vi` | `mask vmsne(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsltu.vv`, `vmsltu.vx` | `mask vmsltu(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmslt.vv`, `vmslt.vx` | `mask vmslt(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsleu.vv`, `vmsleu.vx`, `vmsleu.vi` | `mask vmsleu(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsle.vv`, `vmsle.vx`, `vmsle.vi` | `mask vmsle(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsgtu.vx`, `vmsgtu.vi` | `mask vmsgtu(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |
| `vmsgt.vx`, `vmsgt.vi` | `mask vmsgt(vec<T> vs2, Arg rhs, mask m=all)` | ❌ (vsm 저장 버그) |

> 비교 연산 자체는 정상 동작으로 추정되나, 결과를 vsm으로 저장하는 경로에서 BE/wdata 형성 오류가 발생하여 QEMU와 불일치.

### 6.3 mask boolean ops

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vmandnot.mm` | `mask vmandnot(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmand.mm` | `mask vmand(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmor.mm` | `mask vmor(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmxor.mm` | `mask vmxor(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmornot.mm` | `mask vmornot(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmnand.mm` | `mask vmnand(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmnor.mm` | `mask vmnor(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |
| `vmxnor.mm` | `mask vmxnor(mask vs2, mask vs1)` | ❌ (vsm 저장 버그) |

## 7. Merge / move / register rearrangement

테스트: `vector_fw/unit_test/test_ch7_move.c` — CH7 QEMU/RTL 비교 결과: **0개 불일치 (완전 일치 ✓)**

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vmerge.vvm`, `vmerge.vxm`, `vmerge.vim` | `vec<T> vmerge(mask sel, vec<T> a, Arg b)` | masked form | ✓ |
| `vmv.v.v`, `vmv.v.x`, `vmv.v.i` | `vec<T> vmv(Arg src)` | same decode entry의 unmasked form | ✓ |
| `vmv.s.x` | `vec<T> vmv_s_x(xreg rs1, vec<T> old_vd)` | element 0만 갱신 | ✓ |
| `vmv.x.s` | `xreg vmv_x_s(vec<T> vs2)` | element 0 추출 | ✓ |
| `vmv1r.v`, `vmv2r.v`, `vmv4r.v`, `vmv8r.v` | `vec<T> vmvNr(vec<T> src)` | whole-register move | ✓ |

## 8. Fixed-point / saturating / averaging

테스트: `vector_fw/unit_test/test_ch8_fixed.c` — CH8 QEMU/RTL 비교 결과: **12개 불일치** (vasubu/vasub 반올림 off-by-one, vnclipu/vnclip 팩 순서)

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vsaddu.vv`, `vsaddu.vx`, `vsaddu.vi` | `vec<U> vsaddu(vec<U> vs2, Arg rhs, mask m=all)` | saturating add | ✓ |
| `vsadd.vv`, `vsadd.vx`, `vsadd.vi` | `vec<S> vsadd(vec<S> vs2, Arg rhs, mask m=all)` | saturating add | ✓ |
| `vssubu.vv`, `vssubu.vx` | `vec<U> vssubu(vec<U> vs2, Arg rhs, mask m=all)` | saturating sub | ✓ |
| `vssub.vv`, `vssub.vx` | `vec<S> vssub(vec<S> vs2, Arg rhs, mask m=all)` | saturating sub | ✓ |
| `vaaddu.vv`, `vaaddu.vx` | `vec<U> vaaddu(vec<U> vs2, Arg rhs, mask m=all)` | averaging add | ✓ |
| `vaadd.vv`, `vaadd.vx` | `vec<S> vaadd(vec<S> vs2, Arg rhs, mask m=all)` | averaging add | ✓ |
| `vasubu.vv`, `vasubu.vx` | `vec<U> vasubu(vec<U> vs2, Arg rhs, mask m=all)` | ❌ RTL 버그: vxrm/rounding off-by-one | ❌ |
| `vasub.vv`, `vasub.vx` | `vec<S> vasub(vec<S> vs2, Arg rhs, mask m=all)` | ❌ RTL 버그: vxrm/rounding off-by-one | ❌ |
| `vssrl.vv`, `vssrl.vx`, `vssrl.vi` | `vec<U> vssrl(vec<U> vs2, Arg shamt, mask m=all)` | rounding shift right | ✓ |
| `vssra.vv`, `vssra.vx`, `vssra.vi` | `vec<S> vssra(vec<S> vs2, Arg shamt, mask m=all)` | rounding shift right | ✓ |
| `vnclipu.wv`, `vnclipu.wx`, `vnclipu.wi` | `vec<U> vnclipu(vec<WideU> vs2, Arg shamt, mask m=all)` | ❌ RTL 버그: element pack 순서 불일치 | ❌ |
| `vnclip.wv`, `vnclip.wx`, `vnclip.wi` | `vec<S> vnclip(vec<WideS> vs2, Arg shamt, mask m=all)` | ❌ RTL 버그: element pack 순서 불일치 | ❌ |

## 9. Multiply / MAC

테스트: `vector_fw/unit_test/test_ch9_mul.c` — CH9 QEMU/RTL 비교 결과: **~13개 불일치** (vnmsac element[vl-1] 부호 오류, widening vv multiply LMUL=mf2 elements[0,1])

### 9.1 single-width multiply

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vmulhu.vv`, `vmulhu.vx` | `vec<U> vmulhu(vec<U> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmul.vv`, `vmul.vx` | `vec<T> vmul(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmulhsu.vv`, `vmulhsu.vx` | `vec<T> vmulhsu(vec<T> vs2, Arg rhs, mask m=all)` | ✓ |
| `vmulh.vv`, `vmulh.vx` | `vec<S> vmulh(vec<S> vs2, Arg rhs, mask m=all)` | ✓ |

### 9.2 multiply-accumulate

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vmadd.vv`, `vmadd.vx` | `vec<T> vmadd(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` | | ✓ |
| `vnmsub.vv`, `vnmsub.vx` | `vec<T> vnmsub(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` | | ✓ |
| `vmacc.vv`, `vmacc.vx` | `vec<T> vmacc(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` | | ✓ |
| `vnmsac.vv`, `vnmsac.vx` | `vec<T> vnmsac(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` | ❌ RTL 버그: `.vv` element[vl-1]에서 곱이 2^N 경계일 때 부호 반전 누락 | ❌ |

### 9.3 widening multiply / widening MAC

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vwmulu.vv`, `vwmulu.vx` | `vec<WideU> vwmulu(vec<U> vs2, Arg rhs, mask m=all)` | `.vv` LMUL=mf2: ❌ elements[0,1] 오류; `.vx` ✓ | ❌ |
| `vwmulsu.vv`, `vwmulsu.vx` | `vec<WideT> vwmulsu(vec<T> vs2, Arg rhs, mask m=all)` | 동일 패턴 | ❌ |
| `vwmul.vv`, `vwmul.vx` | `vec<WideS> vwmul(vec<S> vs2, Arg rhs, mask m=all)` | 동일 패턴 | ❌ |
| `vwmaccu.vv`, `vwmaccu.vx` | `vec<WideU> vwmaccu(vec<U> vs2, Arg rhs, vec<WideU> vd, mask m=all)` | 동일 패턴 | ❌ |
| `vwmacc.vv`, `vwmacc.vx` | `vec<WideS> vwmacc(vec<S> vs2, Arg rhs, vec<WideS> vd, mask m=all)` | 동일 패턴 | ❌ |
| `vwmaccus.vx` | `vec<WideS> vwmaccus(vec<U> vs2, xreg rs1, vec<WideS> vd, mask m=all)` | 스칼라 형식: 정상 ✓ | ✓ |
| `vwmaccsu.vv`, `vwmaccsu.vx` | `vec<WideS> vwmaccsu(vec<S> vs2, Arg rhs, vec<WideS> vd, mask m=all)` | `.vv` LMUL=mf2: ❌ | ❌ |

> **widening vv multiply LMUL=mf2 버그**: `.vv` 형식에서 EMUL=mf2일 때 element[0] = element[vl-1]의 곱(잘못된 값), element[1] = 0. elements[2],[3]은 정상. 원인: 파이프라인이 첫 32-bit 청크(element 0,1)를 처리할 때 이전 연산의 stale 데이터를 재사용. `.vx` 스칼라 형식 미영향.

### 9.4 fixed-point multiply

| Instruction | Prototype | 검증 |
| --- | --- | --- |
| `vsmul.vv`, `vsmul.vx` | `vec<S> vsmul(vec<S> vs2, Arg rhs, mask m=all)` | ✓ |

## 10. Slide / gather / compress

테스트: `vector_fw/unit_test/test_ch10_slide.c` — CH10 QEMU/RTL 비교 결과: **1개 불일치** (vcompress.vm element[0])

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vslideup.vi`, `vslideup.vx` | `vec<T> vslideup(vec<T> vs2, Arg offset, mask m=all)` | | ✓ |
| `vslidedown.vi`, `vslidedown.vx` | `vec<T> vslidedown(vec<T> vs2, Arg offset, mask m=all)` | | ✓ |
| `vslide1up.vx` | `vec<T> vslide1up(vec<T> vs2, xreg rs1, mask m=all)` | | ✓ |
| `vslide1down.vx` | `vec<T> vslide1down(vec<T> vs2, xreg rs1, mask m=all)` | | ✓ |
| `vrgather.vv`, `vrgather.vx`, `vrgather.vi` | `vec<T> vrgather(vec<T> vs2, Arg index, mask m=all)` | | ✓ |
| `vcompress.vm` | `vec<T> vcompress(vec<T> vs2, mask select)` | ❌ RTL 버그: element[0]이 이전 vd 잔류값 반환 | ❌ |

## 11. Reduction / element / mask utility

테스트: `vector_fw/unit_test/test_ch11_red_elem.c` — CH11 QEMU/RTL 비교 결과: **1개 불일치** (vfirst.m off-by-one)

| Instruction | Prototype | Notes | 검증 |
| --- | --- | --- | --- |
| `vredsum.vs` | `vec<T> vredsum(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredand.vs` | `vec<T> vredand(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredor.vs` | `vec<T> vredor(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredxor.vs` | `vec<T> vredxor(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredminu.vs` | `vec<T> vredminu(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredmin.vs` | `vec<T> vredmin(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredmaxu.vs` | `vec<T> vredmaxu(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vredmax.vs` | `vec<T> vredmax(vec<T> vs2, vec<T> vs1, mask m=all)` | | ✓ |
| `vwredsumu.vs` | `vec<WideU> vwredsumu(vec<U> vs2, vec<WideU> vs1, mask m=all)` | | ✓ |
| `vwredsum.vs` | `vec<WideS> vwredsum(vec<S> vs2, vec<WideS> vs1, mask m=all)` | | ✓ |
| `vpopc.m` / `vcpop.m` | `xreg vpopc(mask vs2, mask m=all)` | | ✓ |
| `vfirst.m` | `xreg vfirst(mask vs2, mask m=all)` | ❌ RTL 버그: 첫 활성 비트가 index 0일 때 0 대신 1 반환 | ❌ |
| `vid.v` | `vec<U> vid(mask m=all)` | | ✓ |
| `viota.m` | `vec<U> viota(mask vs2, mask m=all)` | | ✓ |

## 12. 검증 결과 요약 및 알려진 RTL 버그

### 챕터별 QEMU vs RTL 비교 결과

실행 방법: `RESULT_SYMBOL=vdata_start ./vector_fw/run_vector_fw.sh <src> qemu|rtl`

| 챕터 | 소스 파일 | 총 라인 | 불일치 | 결과 |
|------|-----------|---------|--------|------|
| CH3 config/CSR | `test_ch3_config.c` | 32 | 4 (lines 8, 17–19) | RTL 버그 |
| CH4 load/store | `test_ch4_loadstore.c` | 64 | 3 (lines 10–12) | RTL 버그 |
| CH5 integer ALU | `test_ch5_int_alu.c` | 128 | 12 (lines 69–80) | RTL 버그 |
| CH6 mask ops | `test_ch6_mask.c` | 128 | ~17 | RTL 버그 |
| CH7 move/merge | `test_ch7_move.c` | 128 | 0 | **완전 일치 ✓** |
| CH8 fixed-point | `test_ch8_fixed.c` | 128 | 12 (lines 25–32, 41–44) | RTL 버그 |
| CH9 multiply/MAC | `test_ch9_mul.c` | 128 | ~13 | RTL 버그 |
| CH10 slide/gather | `test_ch10_slide.c` | 128 | 1 (line 21) | RTL 버그 |
| CH11 reduction | `test_ch11_red_elem.c` | 128 | 1 (line 42) | RTL 버그 |

### 알려진 RTL 버그 목록

| 명령어 / 기능 | 증상 | 원인 (참고 위치) |
|---------------|------|----------------|
| `vsetvl` e32,mf2 | vl=0 반환 (spec: VLMAX=2) | RTL이 SEW=32+LMUL=mf2를 invalid vtype으로 처리하는 것으로 추정 |
| `vxsat` CSR | 포화 연산 후에도 vxsat 미갱신, csrr 항상 0 | RTL ALU → CSR 업데이트 경로 미구현 가능성 |
| `vxrm` CSR | csrw로 쓴 값이 csrr로 읽히지 않음 (항상 0) | RTL CSR read/write 경로 연결 누락 가능성 |
| `vsm.v` | 일부 마스크 저장에서 잘못된 BE(예: 0x3) 또는 sign-extended wdata 생성 | `vproc_vregpack.sv`, `vproc_lsu.sv` 마스크/BE 형성 로직 |
| `vloxei*.v` / `vsoxei*.v` (비제로 인덱스) | 비제로 byte-offset 요소가 0 반환 | RTL이 index를 byte offset 대신 element index로 취급 (× eew_bytes 적용). §4.3 참조 |
| `vnsrl.wv/wx/wi`, `vnsra.wv/wx/wi` | 시프트 미적용, 홀수 레인 0 | OP_NARROWING 경로 `vl_o` 오계산 (`vproc_decoder.sv` line 1577) |
| `vzext.vf2` | 결과 0 출력 | OP_WIDENING 경로 VSEW_32 케이스 미처리 (`vproc_decoder.sv` line 1562) |
| `vsext.vf2` | illegal instruction 예외 → trap_loop | OP_WIDENING에서 EMUL m1→m2, 홀수 vd 레지스터 시 `op_illegal=1` |
| `vasubu.vv/vx`, `vasub.vv/vx` | 일부 음수 결과 off-by-one | `vproc_alu.sv` vxrm/rounding 경로 |
| `vnclipu.*`, `vnclip.*` | element pack 순서 불일치 | `vproc_vregpack.sv`, `vproc_vregunpack.sv` |
| `vnmsac.vv` element[vl-1] | 곱이 2^N 경계일 때 부호 반전 누락 | ALU 올림수/오버플로 처리 오류 추정 |
| widening `.vv` LMUL=mf2: `vwmul*`, `vwmacc*` | element[0]=stale, element[1]=0 | 파이프라인 첫 32-bit 청크 처리 시 stale 데이터 재사용 |
| `vcompress.vm` element[0] | 이전 vd 잔류값 출력 | compress 파이프라인 element[0] 기록 위치 오류 |
| `vfirst.m` (첫 활성 bit=0) | 0 대신 1 반환 (off-by-one) | priority encoder 1-based 인덱스 또는 bit-0 처리 오프셋 |

> **정책**: RTL 코드는 수정하지 않는다. 버그는 이 문서와 해당 펌웨어 소스 파일(`test_ch*.c`)의 주석으로만 기록한다.

### 참고 소스

- `vicuna/rtl/vproc_decoder.sv`
- `vicuna/rtl/vproc_pkg.sv`
- `vicuna/rtl/vproc_alu.sv`
- `vicuna/rtl/vproc_lsu.sv`
- `vicuna/rtl/vproc_vregpack.sv`, `vproc_vregunpack.sv`
- `vicuna/test/alu`, `vicuna/test/lsu`, `vicuna/test/mul`, `vicuna/test/sld`, `vicuna/test/elem`, `vicuna/test/csr`
