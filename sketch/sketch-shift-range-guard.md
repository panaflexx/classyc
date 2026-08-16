# P4 sketch — shift amount range guard (bugs/009)

## Spec

C11 §6.5.7/3: shift count must be in `[0, width)` of the promoted left operand.
Negative or `>= width` is undefined. ClassyC safety mode should trap like
div0 / OOB.

## Today

* `_safety_trap(reason, …)` reasons: 1=OOB, 2=null, 3=div0, 4=UAF  
* Shift ops emit MIR shifts with no runtime check  
* `bugs/009-shift-out-of-range.cy` expects catchable exception — **LIVE**

## Plan

1. Add reason `5 = shift_out_of_range` in `cyexc.h` / `_safety_trap` messages.  
2. In gen for `N_LSHIFT` / `N_RSHIFT` / assign forms (when exceptions/safety on):
   * compute width from left operand type (32 or 64 after promotion);  
   * if count is constant: compile-time error or warning (pedantic → error);  
   * if count is dynamic:  
     `if ((unsigned)count >= width || count < 0) _safety_trap(5, …)`  
     (for unsigned count, only `>= width`).  
3. Prefer throwing `ArithmeticException` or `RuntimeException` when inside try
   (reuse `cy_exc_throw` path already used by `_safety_trap` when active).

## Validation

```sh
./bin/classyc -g -I include bugs/009-shift-out-of-range.cy -eg
# expect: PASS caught wide shift / negative shift
```

## Effort

S–M: gen sites for shifts are localized; constant-fold path is a nice extra.
