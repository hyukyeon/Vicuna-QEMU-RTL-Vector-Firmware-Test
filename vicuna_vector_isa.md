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

## 3. Configuration / CSR

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vsetvl` | `xreg vsetvl(xreg avl, xreg vtype)` | `rd`에 새 VL 반환 |
| `vsetvli` | `xreg vsetvli(xreg avl, uimm vtypei)` | immediate vtype |
| `vsetivli` | `xreg vsetivli(uimm avl, uimm vtypei)` | immediate AVL + vtype |
| `csrrw/csrrs/csrrc` on `vstart` | `xreg csr_vstart(xreg value)` | read/write/set/clear 지원 |
| `csrrw/csrrs/csrrc` on `vxsat` | `xreg csr_vxsat(xreg value)` | read/write/set/clear 지원 |
| `csrrw/csrrs/csrrc` on `vxrm` | `xreg csr_vxrm(xreg value)` | read/write/set/clear 지원 |
| `csrrw/csrrs/csrrc` on `vcsr` | `xreg csr_vcsr(xreg value)` | read/write/set/clear 지원 |
| `csrrs/csrrc` on `vl` | `xreg read_vl()` | read-only |
| `csrrs/csrrc` on `vtype` | `xreg read_vtype()` | read-only |
| `csrrs/csrrc` on `vlenb` | `xreg read_vlenb()` | read-only |

참고:
- `vsetvl x0, x0, rs2` 형태의 **keep VL** 패턴도 RTL에서 처리한다.
- `vtype`는 `SEW=8/16/32`, `LMUL=mf8..m8`만 유효하다.

## 4. Load / Store

### 4.1 기본 load/store

| Instruction | Prototype |
| --- | --- |
| `vle8.v`, `vle16.v`, `vle32.v` | `vec<T> vle(addr base, mask m=all)` |
| `vse8.v`, `vse16.v`, `vse32.v` | `void vse(addr base, vec<T> vs3, mask m=all)` |
| `vlse8.v`, `vlse16.v`, `vlse32.v` | `vec<T> vlse(addr base, xreg stride, mask m=all)` |
| `vsse8.v`, `vsse16.v`, `vsse32.v` | `void vsse(addr base, xreg stride, vec<T> vs3, mask m=all)` |

### 4.2 indexed / segment / whole-register / mask load-store

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vloxei8.v`, `vloxei16.v`, `vloxei32.v` | `vec<T> vloxei(addr base, vec<IdxT> index, mask m=all)` | **spec deviation 있음** (아래 참고) |
| `vsoxei8.v`, `vsoxei16.v`, `vsoxei32.v` | `void vsoxei(addr base, vec<IdxT> index, vec<T> vs3, mask m=all)` | **spec deviation 있음** (아래 참고) |
| indexed encodings (`mop=01/11`) | `vec<T> vlxei(...)/vsxei(...)` | RTL은 ordered/unordered indexed encoding을 모두 수용 |
| `vlseg<nf>e8.v`, `vlseg<nf>e16.v`, `vlseg<nf>e32.v` | `tuple<nf, vec<T>> vlseg(addr base, mask m=all)` | `nf=2..8` |
| `vsseg<nf>e8.v`, `vsseg<nf>e16.v`, `vsseg<nf>e32.v` | `void vsseg(addr base, tuple<nf, vec<T>> data, mask m=all)` | `nf=2..8` |
| `vle8ff.v`, `vle16ff.v`, `vle32ff.v` | `vec<T> vleff(addr base, mask m=all)` | fault-only-first load |
| `vl1r.v`, `vl2r.v`, `vl4r.v`, `vl8r.v` | `vec<T> vlNr(addr base)` | whole-register load |
| `vs1r.v`, `vs2r.v`, `vs4r.v`, `vs8r.v` | `void vsNr(addr base, vec<T> data)` | whole-register store |
| `vlm.v` | `mask vlm(addr base)` | mask load |
| `vsm.v` | `void vsm(addr base, mask data)` | mask store |

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

### 5.1 기본 add/sub/min/max/logic

| Instruction | Prototype |
| --- | --- |
| `vadd.vv`, `vadd.vx`, `vadd.vi` | `vec<T> vadd(vec<T> vs2, Arg rhs, mask m=all)` |
| `vsub.vv`, `vsub.vx` | `vec<T> vsub(vec<T> vs2, Arg rhs, mask m=all)` |
| `vrsub.vx`, `vrsub.vi` | `vec<T> vrsub(vec<T> vs2, Arg rhs, mask m=all)` |
| `vminu.vv`, `vminu.vx` | `vec<T> vminu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmin.vv`, `vmin.vx` | `vec<T> vmin(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmaxu.vv`, `vmaxu.vx` | `vec<T> vmaxu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmax.vv`, `vmax.vx` | `vec<T> vmax(vec<T> vs2, Arg rhs, mask m=all)` |
| `vand.vv`, `vand.vx`, `vand.vi` | `vec<T> vand(vec<T> vs2, Arg rhs, mask m=all)` |
| `vor.vv`, `vor.vx`, `vor.vi` | `vec<T> vor(vec<T> vs2, Arg rhs, mask m=all)` |
| `vxor.vv`, `vxor.vx`, `vxor.vi` | `vec<T> vxor(vec<T> vs2, Arg rhs, mask m=all)` |

### 5.2 shift / narrow shift

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vsll.vv`, `vsll.vx`, `vsll.vi` | `vec<T> vsll(vec<T> vs2, Arg shamt, mask m=all)` | |
| `vsrl.vv`, `vsrl.vx`, `vsrl.vi` | `vec<T> vsrl(vec<T> vs2, Arg shamt, mask m=all)` | |
| `vsra.vv`, `vsra.vx`, `vsra.vi` | `vec<T> vsra(vec<T> vs2, Arg shamt, mask m=all)` | |
| `vnsrl.wv`, `vnsrl.wx`, `vnsrl.wi` | `vec<NarrowT> vnsrl(vec<WideT> vs2, Arg shamt, mask m=all)` | ❌ RTL 미동작 (아래 참고) |
| `vnsra.wv`, `vnsra.wx`, `vnsra.wi` | `vec<NarrowT> vnsra(vec<WideT> vs2, Arg shamt, mask m=all)` | ❌ RTL 미동작 (아래 참고) |

> **vnsrl / vnsra RTL 미동작**: `test_ch5_int_alu` QEMU↔RTL 비교 테스트에서 두 명령어 모두 잘못된 결과를 출력한다.  
> 디코더 엔트리(`{6'b101100/101101, 3'b000/011/100}`, `vproc_decoder.sv` lines 489-517)는 존재하지만, OP_NARROWING 경로(`vproc_decoder.sv` line 1577)의 `vl_o` 계산 버그로 인해 오동작한다.  
>  
> **버그 — vl_o 오계산**  
> OP_NARROWING 시 `vl_o = {vl_i[CFG_VL_W-2:0], 1'b1}` 공식이 적용된다.  
> `vl_i=4`이면 `vl_o = 9`가 되어 `VLMAX=8`(e32,m1, VLEN=256)을 초과한다.  
> 파이프라인이 범위를 벗어난 요소를 처리하면서 결과가 소스 레지스터 원본 값(시프트 미적용)을 일부 레인에만 쓰고 나머지는 0이 되는 잘못된 출력을 생성한다.  
> 실제 RTL 출력(e16,mf2 vnsrl.wi >>2, 입력 {0x11,0x22,0x33,0x44}):  
> `{0x0011, 0x0000, 0x0022, 0x0000}` (기대값: `{0x0004, 0x0008, 0x000C, 0x0011}`)

### 5.3 widening / extension

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vwaddu.vv`, `vwaddu.vx` | `vec<WideU> vwaddu(vec<U> vs2, Arg rhs, mask m=all)` | |
| `vwadd.vv`, `vwadd.vx` | `vec<WideS> vwadd(vec<S> vs2, Arg rhs, mask m=all)` | |
| `vwsubu.vv`, `vwsubu.vx` | `vec<WideU> vwsubu(vec<U> vs2, Arg rhs, mask m=all)` | |
| `vwsub.vv`, `vwsub.vx` | `vec<WideS> vwsub(vec<S> vs2, Arg rhs, mask m=all)` | |
| `vwaddu.wv`, `vwaddu.wx` | `vec<WideU> vwaddu_w(vec<WideU> vs2, Arg rhs, mask m=all)` | |
| `vwadd.wv`, `vwadd.wx` | `vec<WideS> vwadd_w(vec<WideS> vs2, Arg rhs, mask m=all)` | |
| `vwsubu.wv`, `vwsubu.wx` | `vec<WideU> vwsubu_w(vec<WideU> vs2, Arg rhs, mask m=all)` | |
| `vwsub.wv`, `vwsub.wx` | `vec<WideS> vwsub_w(vec<WideS> vs2, Arg rhs, mask m=all)` | |
| `vzext.vf2` | `vec<WideU> vzext_vf2(vec<U> vs2, mask m=all)` | ❌ RTL 미동작 (아래 참고) |
| `vsext.vf2` | `vec<WideS> vsext_vf2(vec<S> vs2, mask m=all)` | ❌ RTL 미동작 (아래 참고) |

> **vzext.vf2 / vsext.vf2 RTL 미동작**: `test_ch5_int_alu` QEMU↔RTL 비교 테스트에서 두 명령어 모두 zero를 출력하거나 illegal instruction 예외를 유발한다. 디코더에 엔트리(`{6'b010010, 3'b010}`, `vproc_decoder.sv` line 657)는 존재하지만 아래 두 가지 버그로 인해 정상 동작하지 않는다.
>
> **버그 1 — vzext.vf2 (zero 출력)**  
> `vzext_vf2_u32m1` 은 컴파일러가 `vtype=e32,m1`로 세팅 후 실행하는 것이 올바른 사용이다.  
> 그러나 디코더의 OP_WIDENING 경로(`vproc_decoder.sv` lines 1562-1566)는 `VSEW_8→VSEW_16`, `VSEW_16→VSEW_32` 만 처리하고, **`VSEW_32` 케이스가 없어** `vsew_o`가 don't-care 상태가 된다. 이로 인해 파이프라인이 잘못된 element width로 동작하여 결과가 0으로 출력된다.
>
> **버그 2 — vsext.vf2 (illegal instruction 예외)**  
> `vtype=e32,m1`에서 OP_WIDENING은 `emul_o`를 m1→m2(EMUL_2)로 증가시킨다. EMUL_2는 목적지 레지스터의 짝수 정렬(even-aligned)을 요구하는데, 컴파일러가 홀수 번호 레지스터(예: v25)를 `vd`로 할당하면 `vd_invalid=1` → `op_illegal=1` → `valid_o=0`이 되어 Ibex가 illegal instruction 예외를 발생시킨다. bare-metal RTL 환경에서는 `trap_loop`에 진입해 이후 명령어가 모두 실행되지 않는다.

## 6. Carry / compare / mask-producing instructions

### 6.1 carry / borrow

| Instruction | Prototype |
| --- | --- |
| `vadc.vv`, `vadc.vx`, `vadc.vi` | `vec<T> vadc(vec<T> vs2, Arg rhs, mask carry_in)` |
| `vsbc.vv`, `vsbc.vx`, `vsbc.vi` | `vec<T> vsbc(vec<T> vs2, Arg rhs, mask borrow_in)` |
| `vmadc.vv`, `vmadc.vx`, `vmadc.vi` | `mask vmadc(vec<T> vs2, Arg rhs, mask carry_in=0)` |
| `vmsbc.vv`, `vmsbc.vx`, `vmsbc.vi` | `mask vmsbc(vec<T> vs2, Arg rhs, mask borrow_in=0)` |

### 6.2 compare

| Instruction | Prototype |
| --- | --- |
| `vmseq.vv`, `vmseq.vx`, `vmseq.vi` | `mask vmseq(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsne.vv`, `vmsne.vx`, `vmsne.vi` | `mask vmsne(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsltu.vv`, `vmsltu.vx` | `mask vmsltu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmslt.vv`, `vmslt.vx` | `mask vmslt(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsleu.vv`, `vmsleu.vx`, `vmsleu.vi` | `mask vmsleu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsle.vv`, `vmsle.vx`, `vmsle.vi` | `mask vmsle(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsgtu.vx`, `vmsgtu.vi` | `mask vmsgtu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmsgt.vx`, `vmsgt.vi` | `mask vmsgt(vec<T> vs2, Arg rhs, mask m=all)` |

### 6.3 mask boolean ops

| Instruction | Prototype |
| --- | --- |
| `vmandnot.mm` | `mask vmandnot(mask vs2, mask vs1)` |
| `vmand.mm` | `mask vmand(mask vs2, mask vs1)` |
| `vmor.mm` | `mask vmor(mask vs2, mask vs1)` |
| `vmxor.mm` | `mask vmxor(mask vs2, mask vs1)` |
| `vmornot.mm` | `mask vmornot(mask vs2, mask vs1)` |
| `vmnand.mm` | `mask vmnand(mask vs2, mask vs1)` |
| `vmnor.mm` | `mask vmnor(mask vs2, mask vs1)` |
| `vmxnor.mm` | `mask vmxnor(mask vs2, mask vs1)` |

## 7. Merge / move / register rearrangement

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vmerge.vvm`, `vmerge.vxm`, `vmerge.vim` | `vec<T> vmerge(mask sel, vec<T> a, Arg b)` | masked form |
| `vmv.v.v`, `vmv.v.x`, `vmv.v.i` | `vec<T> vmv(Arg src)` | same decode entry의 unmasked form |
| `vmv.s.x` | `vec<T> vmv_s_x(xreg rs1, vec<T> old_vd)` | element 0만 갱신 |
| `vmv.x.s` | `xreg vmv_x_s(vec<T> vs2)` | element 0 추출 |
| `vmv1r.v`, `vmv2r.v`, `vmv4r.v`, `vmv8r.v` | `vec<T> vmvNr(vec<T> src)` | whole-register move |

## 8. Fixed-point / saturating / averaging

| Instruction | Prototype | Notes |
| --- | --- | --- |
| `vsaddu.vv`, `vsaddu.vx`, `vsaddu.vi` | `vec<U> vsaddu(vec<U> vs2, Arg rhs, mask m=all)` | saturating add |
| `vsadd.vv`, `vsadd.vx`, `vsadd.vi` | `vec<S> vsadd(vec<S> vs2, Arg rhs, mask m=all)` | saturating add |
| `vssubu.vv`, `vssubu.vx` | `vec<U> vssubu(vec<U> vs2, Arg rhs, mask m=all)` | saturating sub |
| `vssub.vv`, `vssub.vx` | `vec<S> vssub(vec<S> vs2, Arg rhs, mask m=all)` | saturating sub |
| `vaaddu.vv`, `vaaddu.vx` | `vec<U> vaaddu(vec<U> vs2, Arg rhs, mask m=all)` | averaging add |
| `vaadd.vv`, `vaadd.vx` | `vec<S> vaadd(vec<S> vs2, Arg rhs, mask m=all)` | averaging add |
| `vasubu.vv`, `vasubu.vx` | `vec<U> vasubu(vec<U> vs2, Arg rhs, mask m=all)` | averaging sub |
| `vasub.vv`, `vasub.vx` | `vec<S> vasub(vec<S> vs2, Arg rhs, mask m=all)` | averaging sub |
| `vssrl.vv`, `vssrl.vx`, `vssrl.vi` | `vec<U> vssrl(vec<U> vs2, Arg shamt, mask m=all)` | rounding shift right |
| `vssra.vv`, `vssra.vx`, `vssra.vi` | `vec<S> vssra(vec<S> vs2, Arg shamt, mask m=all)` | rounding shift right |
| `vnclipu.wv`, `vnclipu.wx`, `vnclipu.wi` | `vec<U> vnclipu(vec<WideU> vs2, Arg shamt, mask m=all)` | narrowing + saturation |
| `vnclip.wv`, `vnclip.wx`, `vnclip.wi` | `vec<S> vnclip(vec<WideS> vs2, Arg shamt, mask m=all)` | narrowing + saturation |

## 9. Multiply / MAC

### 9.1 single-width multiply

| Instruction | Prototype |
| --- | --- |
| `vmulhu.vv`, `vmulhu.vx` | `vec<U> vmulhu(vec<U> vs2, Arg rhs, mask m=all)` |
| `vmul.vv`, `vmul.vx` | `vec<T> vmul(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmulhsu.vv`, `vmulhsu.vx` | `vec<T> vmulhsu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vmulh.vv`, `vmulh.vx` | `vec<S> vmulh(vec<S> vs2, Arg rhs, mask m=all)` |

### 9.2 multiply-accumulate

| Instruction | Prototype |
| --- | --- |
| `vmadd.vv`, `vmadd.vx` | `vec<T> vmadd(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` |
| `vnmsub.vv`, `vnmsub.vx` | `vec<T> vnmsub(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` |
| `vmacc.vv`, `vmacc.vx` | `vec<T> vmacc(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` |
| `vnmsac.vv`, `vnmsac.vx` | `vec<T> vnmsac(vec<T> vs2, Arg rhs, vec<T> vd, mask m=all)` |

### 9.3 widening multiply / widening MAC

| Instruction | Prototype |
| --- | --- |
| `vwmulu.vv`, `vwmulu.vx` | `vec<WideU> vwmulu(vec<U> vs2, Arg rhs, mask m=all)` |
| `vwmulsu.vv`, `vwmulsu.vx` | `vec<WideT> vwmulsu(vec<T> vs2, Arg rhs, mask m=all)` |
| `vwmul.vv`, `vwmul.vx` | `vec<WideS> vwmul(vec<S> vs2, Arg rhs, mask m=all)` |
| `vwmaccu.vv`, `vwmaccu.vx` | `vec<WideU> vwmaccu(vec<U> vs2, Arg rhs, vec<WideU> vd, mask m=all)` |
| `vwmacc.vv`, `vwmacc.vx` | `vec<WideS> vwmacc(vec<S> vs2, Arg rhs, vec<WideS> vd, mask m=all)` |
| `vwmaccus.vv`, `vwmaccus.vx` | `vec<WideS> vwmaccus(vec<U> vs2, Arg rhs, vec<WideS> vd, mask m=all)` |
| `vwmaccsu.vv`, `vwmaccsu.vx` | `vec<WideS> vwmaccsu(vec<S> vs2, Arg rhs, vec<WideS> vd, mask m=all)` |

### 9.4 fixed-point multiply

| Instruction | Prototype |
| --- | --- |
| `vsmul.vv`, `vsmul.vx` | `vec<S> vsmul(vec<S> vs2, Arg rhs, mask m=all)` |

## 10. Slide / gather / compress

| Instruction | Prototype |
| --- | --- |
| `vslideup.vi`, `vslideup.vx` | `vec<T> vslideup(vec<T> vs2, Arg offset, mask m=all)` |
| `vslidedown.vi`, `vslidedown.vx` | `vec<T> vslidedown(vec<T> vs2, Arg offset, mask m=all)` |
| `vslide1up.vx` | `vec<T> vslide1up(vec<T> vs2, xreg rs1, mask m=all)` |
| `vslide1down.vx` | `vec<T> vslide1down(vec<T> vs2, xreg rs1, mask m=all)` |
| `vrgather.vv`, `vrgather.vx`, `vrgather.vi` | `vec<T> vrgather(vec<T> vs2, Arg index, mask m=all)` |
| `vcompress.vm` | `vec<T> vcompress(vec<T> vs2, mask select)` |

## 11. Reduction / element / mask utility

| Instruction | Prototype |
| --- | --- |
| `vredsum.vs` | `vec<T> vredsum(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredand.vs` | `vec<T> vredand(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredor.vs` | `vec<T> vredor(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredxor.vs` | `vec<T> vredxor(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredminu.vs` | `vec<T> vredminu(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredmin.vs` | `vec<T> vredmin(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredmaxu.vs` | `vec<T> vredmaxu(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vredmax.vs` | `vec<T> vredmax(vec<T> vs2, vec<T> vs1, mask m=all)` |
| `vwredsumu.vs` | `vec<WideU> vwredsumu(vec<U> vs2, vec<WideU> vs1, mask m=all)` |
| `vwredsum.vs` | `vec<WideS> vwredsum(vec<S> vs2, vec<WideS> vs1, mask m=all)` |
| `vpopc.m` | `xreg vpopc(mask vs2, mask m=all)` |
| `vfirst.m` | `xreg vfirst(mask vs2, mask m=all)` |
| `vid.v` | `vec<U> vid(mask m=all)` |
| `viota.m` | `vec<U> viota(mask vs2, mask m=all)` |

## 12. 빠르게 요약한 지원 범위

Vicuna는 대체로 다음 RVV subset을 구현한다.

- **정수 vector ALU**
- **정수 load/store** (unit-stride, strided, indexed, segment, whole-register, mask, fault-only-first)
- **mask compare / boolean**
- **fixed-point saturating / rounding**
- **multiply / MAC / widening MAC**
- **slide / gather / compress**
- **reduction / element utility**

반대로 아래는 이 문서 기준 범위 밖이다.

- floating-point vector
- vector divide / remainder
- 64-bit element 연산
- complex number 전용 연산

### 알려진 RTL 미동작 명령어 (디코더 엔트리 있으나 버그)

| 명령어 | 증상 | 원인 (참고 파일/라인) |
|--------|------|-----------------------|
| `vzext.vf2` | 결과가 0, 스토어 미실행 | OP_WIDENING 경로에서 `VSEW_32` 케이스 미처리 (`vproc_decoder.sv` line 1562) |
| `vsext.vf2` | illegal instruction 예외 → `trap_loop` 진입 | OP_WIDENING이 `emul` m1→m2 증가하여 짝수 정렬 요구, 홀수 `vd`에서 `op_illegal=1` 발생 (`vproc_decoder.sv`) |
| `vnsrl.wv/wx/wi` | 잘못된 결과 (시프트 미적용, 일부 레인 0) | OP_NARROWING 경로의 `vl_o` 계산 오동작으로 유효 요소 수가 잘못 설정됨 (`vproc_decoder.sv` line 1577) |
| `vnsra.wv/wx/wi` | 잘못된 결과 (시프트 미적용, 일부 레인 0) | OP_NARROWING 경로의 동일한 `vl_o` 오계산 버그 (`vproc_decoder.sv` line 1577) |
| `vsm.v` / mask-store path | QEMU와 달리 RTL에서 일부 마스크/스토어가 잘못된 BE(예: 0x3) 또는 sign-extended wdata(예: 0xfffffff5)를 생성하여 메모리 결과 불일치 발생 | vreg pack/unpack ↔ LSU 사이의 마스크/BE·wdata 형성 로직 문제 가능성 (`vproc_vregpack.sv`, `vproc_vregunpack.sv`, `vproc_lsu.sv`) — 테스트: `vector_fw/unit_test/test_ch6_mask.c` (RTL에서 17라인 불일치 관찰) |
| `vloxei*.v` / `vsoxei*.v` (비제로 index) | 비제로 byte-offset 인덱스 요소가 0을 반환 | RTL이 인덱스를 byte offset 대신 element index로 취급하여 내부적으로 `× eew_bytes` 적용. spec은 byte offset을 요구하므로 {4,8,12}→ RTL은 base+16/32/48 (범위 초과)를 읽어 0 반환. 인덱스 0은 정상. Section 4.3 참조. 테스트: `vector_fw/unit_test/test_ch4_loadstore.c` (요소 1-3 불일치) |
| `vwmulu.vv`, `vwmulsu.vv`, `vwmul.vv`, `vwmaccu.vv`, `vwmacc.vv`, `vwmaccsu.vv` (LMUL=mf2) | 요소 [0]이 요소 [vl-1]의 곱(잘못된 값), 요소 [1]이 0 반환; 요소 [2], [3]은 정상 | Fractional LMUL(mf2) 소스 레지스터에서 첫 32-bit 청크(element 0,1)를 처리하는 파이프라인 경로가 마지막 청크(element vl-1)의 연산 결과를 재사용하는 것으로 보임. vv 형식에서만 발생; vx 스칼라 형식은 정상. 테스트: `vector_fw/unit_test/test_ch9_mul.c` |
| `vnmsac.vv` (element vl-1) | 마지막 요소가 `-(vs1×vs2)+vd` 대신 `+(vs1×vs2)+vd` (부호 반전 누락)로 계산됨 | 피연산자 곱이 정확히 2^N에 해당하는 자리올림 경계(예: 4×64=256=2^8)를 지나는 마지막 요소에서 부호 반전 캐리/오버플로 처리 오류 추정. 테스트: `vector_fw/unit_test/test_ch9_mul.c` (line 32: QEMU `0xffffff04`, RTL `0x00000104`) |
| `vcompress.vm` (element 0) | 결과 요소[0]이 올바른 vs2[0] 값(1) 대신 이전 명령에서 vd 레지스터에 남아 있던 값(5)을 반환 | vcompress 파이프라인이 결과 요소[0]에 vs2의 첫 번째 선택 요소를 쓰지 않고 이전 vd 레지스터 잔류값을 그대로 출력. 요소[1..] 및 압축 전체 동작은 정상. 테스트: `vector_fw/unit_test/test_ch10_slide.c` (line 21) |
| `vfirst.m` (첫 번째 활성 요소가 index 0인 경우) | 첫 번째 활성 마스크 비트가 bit 0일 때 0 대신 1 반환 (off-by-one) | 내부 priority encoder 또는 `ffs()` 로직이 1-based 인덱스를 반환하거나 bit 0 처리에 오프셋 오류 존재 추정. vcpop은 정상 동작. 테스트: `vector_fw/unit_test/test_ch11_red_elem.c` (line 42: QEMU `0`, RTL `1`) |

> 위 사항들은 `vector_fw/unit_test/test_ch5_int_alu.c`와 `vector_fw/unit_test/test_ch6_mask.c`를 QEMU와 Verilator(예: `./vector_fw/run_vector_fw.sh ... qemu|rtl`)로 비교한 결과로 확인되었다. RTL의 동작은 위 파일들에 근거해 '미동작(bug)'으로 문서화하였으며, RTL 코드를 직접 수정하지 않고 펌웨어/테스트 측에서 이를 주석으로 표시해 두었다.

> 위 사항은 `vector_fw/unit_test/test_ch5_int_alu.c`를 QEMU와 RTL(Verilator) 양쪽으로 실행하여 결과를 비교한 검증에서 발견되었다.

## 13. 소스 기준

### 검증(Validation) 요약

챕터별 QEMU vs RTL 비교 결과 (운영: `RESULT_SYMBOL=vdata_start ./vector_fw/run_vector_fw.sh <src> qemu|rtl`):

| 챕터 | 소스 파일 | 총 라인 | 불일치 | 결과 |
|------|-----------|---------|--------|------|
| CH4 load/store | `test_ch4_loadstore.c` | 64 | 3 (lines 10–12) | RTL 버그 |
| CH5 integer ALU | `test_ch5_int_alu.c` | 128 | 12 (lines 69–80) | RTL 버그 |
| CH6 mask ops | `test_ch6_mask.c` | 128 | ~17 | RTL 버그 |
| CH7 move/merge | `test_ch7_move.c` | 128 | 0 | **완전 일치** ✓ |
| CH8 fixed-point | `test_ch8_fixed.c` | 128 | 12 (lines 25–32, 41–44) | RTL 버그 |
| CH9 multiply/MAC | `test_ch9_mul.c` | 128 | 16 | RTL 버그 |
| CH10 slide/gather | `test_ch10_slide.c` | 128 | 1 (line 21) | RTL 버그 |
| CH11 reduction | `test_ch11_red_elem.c` | 128 | 1 (line 42) | RTL 버그 |

**CH4** — vloxei32/vsoxei32 non-zero byte-offset 인덱스 불일치. RTL이 인덱스를 byte offset이 아닌 element index로 취급하여 {4,8,12} → out-of-range 주소 → 0 반환. 인덱스 0은 정상 (section 4.3).

**CH5** — vnsrl/vnsra (OP_NARROWING vl_o 버그), vzext.vf2/vsext.vf2 (widening 디코더 버그). 알려진 RTL 버그.

**CH6** — vsm(mask-store) BE/wdata 형성 오류로 cascade 부패 발생. 알려진 RTL 버그.

**CH7** — 전체 128/128 일치. vmv/vmerge/vmvXr 계열 RTL 정상 동작 확인.

**CH8** — vasubu/vasub 결과(lines 25–32): RTL ALU vxrm/round 경로 off-by-one (예: QEMU `0xfffffff9` → RTL `0xfffffff8`). vnclipu/vnclip 관련(lines 41–44): 요소 pack 순서 불일치. Verilator $display 트레이스로 직접 확인. RTL 버그.

**CH9** — 두 가지 RTL 버그:
- `vnmsac.vv` element [vl-1] 부호 오류: line 32 (QEMU `0xffffff04`, RTL `0x00000104`). 곱이 2^N 경계(4×64=0x100)인 마지막 요소에서 부호 반전 누락.
- widening vv multiply LMUL=mf2 elements [0],[1] 오류: lines 37–38, 41–42, 45–46, 49–50, 53–54, 61–62. 요소[0] = 요소[vl-1]의 곱으로 오염, 요소[1] = 0. vwmulu/vwmulsu/vwmul/vwmaccu/vwmacc/vwmaccsu (vv 형식) 전체 해당; vwmaccus.vx(스칼라 형식)은 정상.

**CH10** — vcompress.vm element [0] 오류: line 21 (QEMU `1`, RTL `5`). 요소[0]에 올바른 vs2[0] 대신 이전 vd 레지스터 잔류값이 출력됨. RTL 버그.

**CH11** — vfirst.m off-by-one: line 42 (QEMU `0`, RTL `1`). 첫 번째 활성 요소가 index 0일 때 0 대신 1 반환. vcpop은 정상. RTL 버그.

> 모든 불일치는 RTL 버그로 분류되며, RTL 코드는 수정하지 않고 문서와 펌웨어 소스에 주석으로 기록한다.


- `vicuna/rtl/vproc_decoder.sv`
- `vicuna/rtl/vproc_pkg.sv`
- `vicuna/test/alu`
- `vicuna/test/lsu`
- `vicuna/test/mul`
- `vicuna/test/sld`
- `vicuna/test/elem`
- `vicuna/test/csr`
