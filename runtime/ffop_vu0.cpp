// FFOP's VU0 microprograms (the MPG of the VIF0 chain at 0x1EDA30, uploaded to VU0 address 0 by 0x193CC0) as native
// C++, instruction by instruction on the calling context's VU0 registers. Entry 0x0 is the cosine and sine of vf31.x;
// 0xD8, 0x108 and 0x138 turn the matrix rows vf4..vf6 about one axis by vf31.x; 0x168 turns them by vf31.y, then by
// vf31.x about another axis. All share the sine and cosine routine at 0x20.
#include "ffop_port.h"

#include "bozo/context.h"
#include "bozo/ops_vu0.h"

#include <cstdint>

namespace ffop::vu0
{
    namespace
    {
        namespace v = bozo::vu0;
        using Vec = v::Vec;

        constexpr uint32_t X = 8, Y = 4, Z = 2, W = 1, XZ = X | Z, XY = X | Y, ZW = Z | W, XYZ = 14, XYZW = 15;
        constexpr uint32_t kHalfPi = 0x3FC90FDBu;

        Vec &vf(R5900Context *ctx, int n) { return ctx->vu0_vf[n]; }
        Vec imm(uint32_t bits) { return v::broadcast(bits); }

        // 0x20 .. 0xD0: each field of vf31 to its sine's approximation (the caller subtracts pi / 2 from x and z
        // through the I it loads, which makes those two the cosine). Leaves the matrix rows in vf28 (vf4), vf29 (vf5),
        // vf30 (vf6) as they were on entry.
        void sineCosine(R5900Context *ctx, Vec &acc)
        {
            Vec &f31 = vf(ctx, 31), &f26 = vf(ctx, 26), &f27 = vf(ctx, 27);
            Vec &f28 = vf(ctx, 28), &f29 = vf(ctx, 29), &f30 = vf(ctx, 30);
            const Vec f0 = vf(ctx, 0);
            v::sub(ctx, f31, f31, imm(kHalfPi), XZ);
            v::abs(f31, f31, XYZW);
            v::max(f26, f0, v::bc(f0, 3), XYZW);
            v::mul(ctx, acc, f31, imm(0xBE22F983u), XYZW);
            v::msub(ctx, acc, acc, f26, imm(0x4B400000u), XYZW);
            v::madd(ctx, acc, acc, f26, imm(0x4B400000u), XYZW);
            v::msub(ctx, acc, acc, f31, imm(0xBE22F983u), XYZW);
            v::msub(ctx, f31, acc, f26, imm(0x3F000000u), XYZW);
            v::abs(f31, f31, XYZW);
            v::sub(ctx, f31, f31, imm(0x3E800000u), XYZW);
            v::mul(ctx, f26, f31, f31, XYZW);
            v::mul(ctx, f30, f31, imm(0xC2992661u), XYZW);
            v::mul(ctx, f28, f31, imm(0xC2255DE0u), XYZW);
            v::mul(ctx, f29, f31, imm(0x42A33457u), XYZW);
            v::mul(ctx, f27, f26, f26, XYZW);
            v::mul(ctx, f30, f30, f26, XYZW);
            v::mul(ctx, acc, f28, f26, XYZW);
            v::mul(ctx, f28, f31, imm(0x421ED7B7u), XYZW);
            v::mul(ctx, f26, f27, f27, XYZW);
            v::madd(ctx, acc, acc, f30, f27, XYZW);
            f30 = vf(ctx, 6);
            v::madd(ctx, acc, acc, f29, f27, XYZW);
            f29 = vf(ctx, 5);
            v::madd(ctx, acc, acc, f31, imm(0x40C90FDAu), XYZW);
            v::madd(ctx, f31, acc, f28, f26, XYZW);
            f28 = vf(ctx, 4);
        }

        // The entries' common start: vf31 = (x, x, z, z), then the routine (x and z get the cosine).
        void angles(R5900Context *ctx, Vec &acc)
        {
            Vec &f31 = vf(ctx, 31);
            const Vec f0 = vf(ctx, 0);
            v::add(ctx, f31, f0, v::bc(f31, 0), Y);
            v::mul(ctx, f31, f0, v::bc(f31, 2), W);
            sineCosine(ctx, acc);
        }

        // Rows a and b (xyz) turned: a = a * vf31[s] + b * vf31[c], b = b * vf31[s] - oldA * vf31[c], oldA the copy
        // of row a the routine left (pairs 0x118 .. 0x130 and their like).
        void turn(R5900Context *ctx, Vec &acc, int a, int b, int oldA, int cosField, int sinField)
        {
            Vec &f31 = vf(ctx, 31);
            v::mul(ctx, acc, vf(ctx, a), v::bc(f31, sinField), XYZ);
            v::madd(ctx, vf(ctx, a), acc, vf(ctx, b), v::bc(f31, cosField), XYZ);
            v::mul(ctx, acc, vf(ctx, b), v::bc(f31, sinField), XYZ);
            v::msub(ctx, vf(ctx, b), acc, vf(ctx, oldA), v::bc(f31, cosField), XYZ);
        }

        void finish(R5900Context *ctx, const Vec &acc, uint32_t returnTo)
        {
            ctx->vu0_acc = acc;
            ctx->vu0_i_bits = 0x40C90FDAu;
            ctx->vi[15] = returnTo;
        }

        // 0x0: vf31.x and vf31.y to the cosine and sine of vf31.x (z and w as the routine leaves them).
        void sineCosineOfX(uint8_t *, R5900Context *ctx, Runtime *)
        {
            Vec acc = ctx->vu0_acc;
            Vec &f31 = vf(ctx, 31);
            v::add(ctx, f31, vf(ctx, 0), v::bc(f31, 0), Y);
            sineCosine(ctx, acc);
            finish(ctx, acc, 0x10u);
        }

        void turnX(uint8_t *, R5900Context *ctx, Runtime *)
        {
            Vec acc = ctx->vu0_acc;
            angles(ctx, acc);
            turn(ctx, acc, 5, 6, 29, 0, 1);
            finish(ctx, acc, 0xE8u);
        }

        void turnY(uint8_t *, R5900Context *ctx, Runtime *)
        {
            Vec acc = ctx->vu0_acc;
            angles(ctx, acc);
            turn(ctx, acc, 6, 4, 30, 0, 1);
            finish(ctx, acc, 0x118u);
        }

        void turnZ(uint8_t *, R5900Context *ctx, Runtime *)
        {
            Vec acc = ctx->vu0_acc;
            angles(ctx, acc);
            turn(ctx, acc, 4, 5, 28, 0, 1);
            finish(ctx, acc, 0x148u);
        }

        // 0x168: about one axis by vf31.y (and vf31.z, its pair) from rows vf4 / vf5 (by w and z) and vf6 / vf4 (by
        // y and x), then by the saved vf31.x about the vf5 / vf6 axis.
        void turnTwo(uint8_t *, R5900Context *ctx, Runtime *)
        {
            Vec acc = ctx->vu0_acc;
            Vec &f31 = vf(ctx, 31), &f25 = vf(ctx, 25);
            const Vec f0 = vf(ctx, 0);
            v::add(ctx, f25, f0, v::bc(f31, 0), XY);
            f31.u[3] = 0;
            v::add(ctx, f31, f0, v::bc(f31, 1), X);
            v::add(ctx, f31, f31, v::bc(f31, 2), W);
            sineCosine(ctx, acc);
            turn(ctx, acc, 4, 5, 28, 2, 3);
            v::mul(ctx, acc, vf(ctx, 6), v::bc(f31, 1), XYZ);
            const Vec sine = f31;
            f31.u[2] = f31.u[3] = 0;
            v::madd(ctx, vf(ctx, 6), acc, vf(ctx, 4), v::bc(sine, 0), XYZ);
            v::mul(ctx, acc, vf(ctx, 4), v::bc(sine, 1), XYZ);
            v::msub(ctx, vf(ctx, 4), acc, vf(ctx, 30), v::bc(sine, 0), XYZ);
            f31.u[0] = f25.u[0];
            f31.u[1] = f25.u[1];
            sineCosine(ctx, acc);
            turn(ctx, acc, 5, 6, 29, 0, 1);
            finish(ctx, acc, 0x1C0u);
        }
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.vu0Program(0x0u, &sineCosineOfX);
        runtime.vu0Program(0xD8u, &turnX);
        runtime.vu0Program(0x108u, &turnY);
        runtime.vu0Program(0x138u, &turnZ);
        runtime.vu0Program(0x168u, &turnTwo);
    }
}
