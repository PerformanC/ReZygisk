/* INFO: Some libc functions, such as memcpy, have hardware-accelerated paths,
           noticeably SIMD/NEON.

         SIMD/NEON have their own set of registers, which are caller-saved. That
           means that if a function uses them, it is expected to restore them before
           returning. However, libc functions do not restore them, and they leave
           data in them.

         To address this, we provide a function to clear these registers. To assure
           we will not leave the string data in the registers, which can be abused
           to detect ReZygisk.
*/

#ifndef REGISTERS_H
#define REGISTERS_H

#include <stddef.h>

#ifdef __aarch64__
  /* INFO: In ARM64, we clear the following registers:
             NEON: q0, q1, ..., q7 
             GPR : x6, x7, x11, x12, x13, x14
  */
  __attribute__((always_inline))
  static inline void registers_clear(void) {
    __asm__ volatile(
      "mov x6, xzr\n"
      "mov x7, xzr\n"
      "mov x11, xzr\n"
      "mov x12, xzr\n"
      "mov x13, xzr\n"
      "mov x14, xzr\n"
      "mov x15, xzr\n"
      "mov x16, xzr\n"
      "mov x17, xzr\n"
      "movi v0.16b, #0\n"
      "movi v1.16b, #0\n"
      "movi v2.16b, #0\n"
      "movi v3.16b, #0\n"
      "movi v4.16b, #0\n"
      "movi v5.16b, #0\n"
      "movi v6.16b, #0\n"
      "movi v7.16b, #0\n"
      : : : "x6","x7","x11","x12","x13","x14","x15","x16","x17",
            "v0","v1","v2","v3","v4","v5","v6","v7"
    );
  }
#elif defined(__arm__)
  /* INFO: In ARM32, we clear the following registers:
             NEON: q0, q1, q2, q3
             GPR : r0, r1, r2, r3, r12 (ip)
  */
  __attribute__((always_inline))
  static inline void registers_clear(void) {
    __asm__ volatile(
      "mov r0, #0\n"
      "mov r1, #0\n"
      "mov r2, #0\n"
      "mov r3, #0\n"
      "mov ip, #0\n"
      "veor q0, q0, q0\n"
      "veor q1, q1, q1\n"
      "veor q2, q2, q2\n"
      "veor q3, q3, q3\n"
      "veor q8, q8, q8\n"
      "veor q9, q9, q9\n"
      "veor q10, q10, q10\n"
      "veor q11, q11, q11\n"
      "veor q12, q12, q12\n"
      "veor q13, q13, q13\n"
      "veor q14, q14, q14\n"
      "veor q15, q15, q15\n"
      : : : "r0","r1","r2","r3","ip",
            "q0","q1","q2","q3","q8","q9","q10","q11",
            "q12","q13","q14","q15"
    );
  }
#elif defined(__x86_64__)
  /* INFO: In x64, we clear the following registers:
             SSE: xmm0, xmm1, ..., xmm15
             GPR: rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11
  */
  __attribute__((always_inline))
  static inline void registers_clear(void) {
    __asm__ volatile(
      "xor %%rax, %%rax\n"
      "xor %%rcx, %%rcx\n"
      "xor %%rdx, %%rdx\n"
      "xor %%rsi, %%rsi\n"
      "xor %%rdi, %%rdi\n"
      "xor %%r8, %%r8\n"
      "xor %%r9, %%r9\n"
      "xor %%r10, %%r10\n"
      "xor %%r11, %%r11\n"
      "xorps %%xmm0, %%xmm0\n"
      "xorps %%xmm1, %%xmm1\n"
      "xorps %%xmm2, %%xmm2\n"
      "xorps %%xmm3, %%xmm3\n"
      "xorps %%xmm4, %%xmm4\n"
      "xorps %%xmm5, %%xmm5\n"
      "xorps %%xmm6, %%xmm6\n"
      "xorps %%xmm7, %%xmm7\n"
      "xorps %%xmm8, %%xmm8\n"
      "xorps %%xmm9, %%xmm9\n"
      "xorps %%xmm10, %%xmm10\n"
      "xorps %%xmm11, %%xmm11\n"
      "xorps %%xmm12, %%xmm12\n"
      "xorps %%xmm13, %%xmm13\n"
      "xorps %%xmm14, %%xmm14\n"
      "xorps %%xmm15, %%xmm15\n"
      : : : "rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11",
            "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7",
            "xmm8","xmm9","xmm10","xmm11","xmm12","xmm13","xmm14","xmm15"
    );
  }
#elif defined(__i386__)
  /* INFO: In x86 (32-bit), only 32-bit GPRs and xmm0..xmm7 exist.
             GPR: eax, ecx, edx (caller-saved)
             SSE: xmm0, xmm1, ..., xmm7
  */
  __attribute__((always_inline))
  static inline void registers_clear(void) {
    __asm__ volatile(
      "xor %%eax, %%eax\n"
      "xor %%ecx, %%ecx\n"
      "xor %%edx, %%edx\n"
      "xorps %%xmm0, %%xmm0\n"
      "xorps %%xmm1, %%xmm1\n"
      "xorps %%xmm2, %%xmm2\n"
      "xorps %%xmm3, %%xmm3\n"
      "xorps %%xmm4, %%xmm4\n"
      "xorps %%xmm5, %%xmm5\n"
      "xorps %%xmm6, %%xmm6\n"
      "xorps %%xmm7, %%xmm7\n"
      : : : "eax","ecx","edx",
            "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"
    );
  }
#endif

#endif /* REGISTERS_H */
