/* 4x4 matrix functions Aurora declares (mtx44ext.h) but does not define.
 * Same arithmetic as the 2004 SDK's C versions (libs/dolsdk2004/src/mtx/
 * mtx44.c, mtx44vec.c), which the headless backend compiles. The game's
 * PSMTX44* names map here (port/include/aurora_overlay/dolphin/mtx.h). */
#include <dolphin/mtx.h>
#include <string.h>

void C_MTX44Identity(Mtx44 m) {
    m[0][0] = 1.0f; m[0][1] = 0.0f; m[0][2] = 0.0f; m[0][3] = 0.0f;
    m[1][0] = 0.0f; m[1][1] = 1.0f; m[1][2] = 0.0f; m[1][3] = 0.0f;
    m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = 1.0f; m[2][3] = 0.0f;
    m[3][0] = 0.0f; m[3][1] = 0.0f; m[3][2] = 0.0f; m[3][3] = 1.0f;
}

void C_MTX44Copy(const Mtx44 src, Mtx44 dst) {
    if (src != (const f32(*)[4])dst) {
        memmove(dst, src, sizeof(Mtx44));
    }
}

void C_MTX44Concat(const Mtx44 a, const Mtx44 b, Mtx44 ab) {
    Mtx44 tmp;
    f32(*m)[4] = (ab == (f32(*)[4])a || ab == (f32(*)[4])b) ? tmp : ab;
    int i;

    for (i = 0; i < 4; i++) {
        m[i][0] = a[i][0] * b[0][0] + a[i][1] * b[1][0] + a[i][2] * b[2][0] + a[i][3] * b[3][0];
        m[i][1] = a[i][0] * b[0][1] + a[i][1] * b[1][1] + a[i][2] * b[2][1] + a[i][3] * b[3][1];
        m[i][2] = a[i][0] * b[0][2] + a[i][1] * b[1][2] + a[i][2] * b[2][2] + a[i][3] * b[3][2];
        m[i][3] = a[i][0] * b[0][3] + a[i][1] * b[1][3] + a[i][2] * b[2][3] + a[i][3] * b[3][3];
    }
    if (m == tmp) {
        memcpy(ab, tmp, sizeof(Mtx44));
    }
}

void C_MTX44Trans(Mtx44 m, f32 xT, f32 yT, f32 zT) {
    C_MTX44Identity(m);
    m[0][3] = xT;
    m[1][3] = yT;
    m[2][3] = zT;
}

void C_MTX44Scale(Mtx44 m, f32 xS, f32 yS, f32 zS) {
    C_MTX44Identity(m);
    m[0][0] = xS;
    m[1][1] = yS;
    m[2][2] = zS;
}

void C_MTX44MultVec(const Mtx44 m, const Vec* src, Vec* dst) {
    Vec tmp;
    f32 w;

    tmp.x = m[0][0] * src->x + m[0][1] * src->y + m[0][2] * src->z + m[0][3];
    tmp.y = m[1][0] * src->x + m[1][1] * src->y + m[1][2] * src->z + m[1][3];
    tmp.z = m[2][0] * src->x + m[2][1] * src->y + m[2][2] * src->z + m[2][3];
    w = m[3][0] * src->x + m[3][1] * src->y + m[3][2] * src->z + m[3][3];
    w = 1.0f / w;

    dst->x = tmp.x * w;
    dst->y = tmp.y * w;
    dst->z = tmp.z * w;
}
