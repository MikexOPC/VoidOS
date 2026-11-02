; ====== audit_fixture ======

; Intel SDM Vol.2, instruction entries PXOR, FINIT/FNINIT and VZEROUPPER:
; https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
; Object-only negative audit fixtures; these instructions are never executed.
bits 64
section .text
global audit_fixture
audit_fixture:
%ifdef AUDIT_SIMD
    pxor xmm0, xmm0
%endif
%ifdef AUDIT_X87
    fninit
%endif
%ifdef AUDIT_AVX
    vzeroupper
%endif
    ret
