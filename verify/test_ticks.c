#include <stdint.h>
#include <stdio.h>
#include "nt_const.h"
#define SYSCLOCK_ARM11 268111856ull
static uint64_t orig(uint64_t t) { return (t / SYSCLOCK_ARM11) * 46875000ull + (t % SYSCLOCK_ARM11) * 46875000ull / SYSCLOCK_ARM11; }
static inline uint64_t mulhi64(uint64_t a, uint64_t b) {
    uint32_t a0 = (uint32_t)a, a1 = (uint32_t)(a >> 32), b0 = (uint32_t)b, b1 = (uint32_t)(b >> 32);
    uint64_t p00 = (uint64_t)a0 * b0, p01 = (uint64_t)a0 * b1, p10 = (uint64_t)a1 * b0, p11 = (uint64_t)a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}
static uint64_t fast(uint64_t t) {
    uint64_t q = mulhi64(t, NT_M);
    uint64_t r = t * NT_N - q * NT_C;      /* exact remainder, or remainder + C if q is one low */
    if (r >= NT_C) q++;
    return q;
}
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
int main(void) {
    long n = 0, bad = 0, off = 0;
    /* uptime range: 0 .. 2^50 ticks (= 49 days at 268 MHz); orig overflows nowhere in this range */
    for (int bits = 1; bits <= 50; bits++)
        for (int k = 0; k < 400000; k++) {
            uint64_t t = rnd() & ((bits == 64 ? ~0ull : (1ull << bits)) - 1);
            n++; uint64_t a = orig(t), b = fast(t);
            if (a != b) { if (!bad) printf("first mismatch t=%llu orig=%llu fast=%llu\n", (unsigned long long)t, (unsigned long long)a, (unsigned long long)b); bad++; }
        }
    /* boundaries: just around the points where floor(t*N/C) steps, for many q */
    for (uint64_t q = 1; q < 3000000; q += 7) {
        uint64_t t0 = (q * NT_C + NT_N - 1) / NT_N;   /* smallest t with floor(tN/C) >= q */
        for (int d = -2; d <= 2; d++) { uint64_t t = t0 + d; n++; if (orig(t) != fast(t)) bad++; }
    }
    /* monotonic over a dense run */
    uint64_t prev = 0; for (uint64_t t = 1ull << 40; t < (1ull << 40) + 5000000; t++) { uint64_t v = fast(t); if (v < prev) bad++; prev = v; n++; }
    printf("compared %ld values, mismatches: %ld\n", n, bad);
    printf(bad ? "RESULT: NOT EQUAL\n" : "RESULT: identical to original formula\n");
    return bad != 0;
}
