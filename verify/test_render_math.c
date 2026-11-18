#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((rnd() & 0xFFFFFF) / 16777216.0f); }
static int32_t bits(float f) { int32_t i; memcpy(&i, &f, 4); return i; }

typedef struct { float x, y, z, w; } V;
/* ---- ORIGINAL cull (gfx_sp_tri1, TARGET_N3DS path): returns 1 if culled ---- */
static int cull_orig(const V*a, const V*b, const V*c, int cull_front) {
    const float r1 = 1.0f / a->w, r2 = 1.0f / b->w, r3 = 1.0f / c->w;
    const float dx1 = a->x * r1 - b->x * r2, dy1 = a->y * r1 - b->y * r2;
    const float dx2 = c->x * r3 - b->x * r2, dy2 = c->y * r3 - b->y * r2;
    const float cross = dx1 * dy2 - dy1 * dx2;
    const int32_t oops = bits(cross) ^ bits(a->w) ^ bits(b->w) ^ bits(c->w) ^ ((cull_front ? 0x200 : 0) << 22);
    return oops >= 0;
}
/* ---- PROPOSED: sign of the 3x3 determinant; the w signs cancel, no divides ---- */
static int cull_det(const V*a, const V*b, const V*c, int cull_front) {
    const float det = a->x * (b->y * c->w - c->y * b->w)
                    - a->y * (b->x * c->w - c->x * b->w)
                    + a->w * (b->x * c->y - c->x * b->y);
    const int32_t oops = ~bits(det) ^ ((cull_front ? 0x200 : 0) << 22);
    return oops >= 0;
}
static float area_ndc(const V*a, const V*b, const V*c) {   /* |cross| in NDC for characterising mismatches */
    float X1=a->x/a->w,Y1=a->y/a->w,X2=b->x/b->w,Y2=b->y/b->w,X3=c->x/c->w,Y3=c->y/c->w;
    return fabsf((X1-X2)*(Y3-Y2)-(Y1-Y2)*(X3-X2));
}

int main(void) {
    /* 1. colour LUT must be bit-identical to the per-vertex int->float multiply */
    static float lut[256]; int lutbad = 0;
    for (int i = 0; i < 256; i++) lut[i] = (float)i * (1.0f / 255.0f);
    for (int i = 0; i < 256; i++) { volatile uint8_t c = (uint8_t)i; float a = c * (1.0f / 255.0f); if (bits(a) != bits(lut[i])) lutbad++; }
    printf("colour LUT: %d / 256 differ\n", lutbad);

    /* 2. cull test with a realistic generator: points are made in NDC (|X|,|Y| <= 1.5, i.e. on or near
     *    the screen), then scaled by w. Slivers are built in NDC with a tiny perpendicular offset. */
    long n = 0, diff = 0, big = 0; double worst = 0; long hist[9] = {0};
    long det_wrong = 0, orig_wrong = 0;
    for (long t = 0; t < 40000000; t++) {
        V v[3]; float X[3], Y[3];
        int mixed = (rnd() % 100) < 15;
        for (int i = 0; i < 3; i++) {
            float w = frand(0.5f, 4000.0f); if (mixed && (rnd() & 1)) w = -w;
            v[i].w = w; X[i] = frand(-1.5f, 1.5f); Y[i] = frand(-1.5f, 1.5f);
        }
        if (rnd() % 4 == 0) {                         /* sliver: v2 almost on segment v0-v1 in NDC */
            float s = frand(-0.2f, 1.2f), px = -(Y[1] - Y[0]), py = (X[1] - X[0]);
            float e = frand(-1.0f, 1.0f) * powf(10.0f, -(float)(rnd() % 7));   /* offset 1 .. 1e-6 */
            X[2] = X[0] + (X[1] - X[0]) * s + px * e; Y[2] = Y[0] + (Y[1] - Y[0]) * s + py * e;
        }
        for (int i = 0; i < 3; i++) { v[i].x = X[i] * v[i].w; v[i].y = Y[i] * v[i].w; v[i].z = 0; }
        int cf = rnd() & 1;
        n++;
        int o = cull_orig(&v[0], &v[1], &v[2], cf), d = cull_det(&v[0], &v[1], &v[2], cf);
        if (o != d) {
            diff++;
            /* double-precision truth from the float vertices */
            double r1 = 1.0/v[0].w, r2 = 1.0/v[1].w, r3 = 1.0/v[2].w;
            double dx1 = v[0].x*r1 - v[1].x*r2, dy1 = v[0].y*r1 - v[1].y*r2, dx2 = v[2].x*r3 - v[1].x*r2, dy2 = v[2].y*r3 - v[1].y*r2;
            double cr = dx1*dy2 - dy1*dx2; int sn = cr < 0;
            int truth = !(sn ^ (v[0].w<0) ^ (v[1].w<0) ^ (v[2].w<0) ^ cf);
            if (o != truth) orig_wrong++; if (d != truth) det_wrong++;
            float a = (float)fabs(cr); int dec = a <= 0 ? 8 : (int)floorf(-log10f(a)); if (dec < 0) dec = 0; if (dec > 8) dec = 8; hist[dec]++;
            if (a > 1e-4f) big++; if (a > worst) worst = a;
        }
    }
    printf("cull: %ld triangles, %ld disagree (%.5f%%)\n", n, diff, 100.0 * diff / n);
    printf("  disagreeing with |NDC cross| > 1e-4: %ld;  largest disagreeing |cross| = %.3g\n", big, worst);
    printf("  vs double-precision truth: original wrong %ld, proposed wrong %ld\n", orig_wrong, det_wrong);
    printf("  disagreement count by |cross| decade (>=1, 1e-1, ..., 1e-7, <=1e-8):");
    for (int i = 0; i < 9; i++) printf(" %ld", hist[i]); printf("\n");
    return 0;
}
