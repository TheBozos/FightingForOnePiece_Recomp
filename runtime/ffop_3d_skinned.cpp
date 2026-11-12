// FFOP's skinned meshes natively: the VU1 programs 0x198B68(ctx, mesh, packet, list, index, pass, skeleton, program, key)
// selects, redone at that call. The palette is the game's: bone k (the k-th node whose parent index is not negative) is
// node(4x4) * bone world matrix * the VU0 matrix on entry * camera, with the per-bone alpha of programs 10-13 in [3].w.
// - 1 / 10 (blobs 0x1F1D80, 0x1F2E20): textured strips of six-qword vertices (position, normal, colour, ST, four bone
//   offsets of 7 * slot, four weights), up to four bones, unlit (colour times the tint), no fog;
// - 5 / 11 (0x1F7340, 0x1F86D0): the same strips' silhouette: each inner edge whose two triangles face opposite ways
//   becomes a quad 4 pixels wide, black at alpha 0x20 (11: 32 times each end's bone alpha);
// - 6 / 12 (0x1F9A10, 0x1FADD0): such a quad for every edge of an edge list;
// - 7 / 13 (0x1FBF10, 0x1FD150): for every (wing, A, B, wing) record whose two triangles face opposite ways.
// Triangles touching a vertex outside the guard band are dropped, as no clipper runs.
#include "ffop_port.h"

#include "ffop_3d.h"
#include "ffop_functions.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ffop::draw3d
{
    namespace
    {
        constexpr uint32_t kSkinned = 0x00198B68u;

        constexpr uint32_t kGuard = 0x002B3690u;
        constexpr uint32_t kScale = 0x002B36A0u;
        constexpr uint32_t kOffset = 0x002B36B0u;
        constexpr uint32_t kOutlinePrim = 0x44u;
        constexpr float kOutlineHalfWidth = 2.0f;
        constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        Vec4 asVec(const Qword &q) { return {asFloat(q[0]), asFloat(q[1]), asFloat(q[2]), asFloat(q[3])}; }

        struct Screen
        {
            float x = 0, y = 0, z = 0, alpha = 1;
            bool inside = false;
        };

        struct Skin
        {
            std::vector<Mat4> palette;
            Vec4 tint;
            float alpha = 1;
            uint32_t program = 1;
            Projection proj;
            float matrix[16] = {};
            GsRegs gs;

            uint32_t bones(uint32_t type) const { return type == 0u ? 1u : type == 2u ? 2u : type == 4u ? 3u : 4u; }
            bool perBoneAlpha() const { return program >= 10u; }

            // Sum of weight * palette row over the vertex's bones (one bone: its rows as they are).
            Mat4 blend(const Qword &refs, const Vec4 &weights, uint32_t count) const
            {
                const auto row = [&](uint32_t i) -> const Mat4 & {
                    const uint32_t slot = refs[i] / 7u;
                    static const Mat4 zero{};
                    return slot < palette.size() ? palette[slot] : zero;
                };
                if (count == 1u)
                    return row(0);
                const float w[4] = {weights.x, weights.y, weights.z, weights.w};
                Mat4 m{};
                for (uint32_t i = 0; i < count; ++i)
                {
                    const Mat4 &p = row(i);
                    for (int r = 0; r < 4; ++r)
                    {
                        m[r].x += w[i] * p[r].x;
                        m[r].y += w[i] * p[r].y;
                        m[r].z += w[i] * p[r].z;
                        m[r].w += w[i] * p[r].w;
                    }
                }
                return m;
            }

            Screen screen(const Vec4 &v, float boneAlpha) const
            {
                Screen s;
                s.x = proj.screenX(v);
                s.y = proj.screenY(v);
                s.z = proj.screenZ(v);
                s.inside = proj.inside(v);
                s.alpha = boneAlpha;
                return s;
            }

            void strip(const std::vector<Qword> &in, size_t at, uint32_t count, const Qword &head, Builder &out) const;
            void main(const std::vector<Qword> &in, Builder &out);
            void silhouette(const std::vector<Qword> &in, Builder &out) const;
            void edges(const std::vector<Qword> &in, Builder &out) const;
            void quad(const Screen &a, const Screen &b, Builder &out) const;
        };

        void Skin::strip(const std::vector<Qword> &in, size_t at, uint32_t count, const Qword &head, Builder &out) const
        {
            const uint32_t prim = (head[1] >> 15) & 0x7FFu, nbones = bones((head[3] & 0xFFFFu) - 4u);
            Vertex verts[3];
            bool inside[3] = {};
            Vertex first;
            for (uint32_t k = 0; k < count; ++k)
            {
                const size_t base = at + size_t(k) * 6u;
                const Vec4 q0 = asVec(in[base & 1023u]), q2 = asVec(in[(base + 2u) & 1023u]), q3 = asVec(in[(base + 3u) & 1023u]);
                const Mat4 m = blend(in[(base + 4u) & 1023u], asVec(in[(base + 5u) & 1023u]), nbones);
                const Vec4 v = point(m, q0.x, q0.y, q0.z);
                const float a = q2.w * alpha * (perBoneAlpha() ? m[3].w : 1.0f);
                Vertex o;
                o.x = v.x;
                o.y = v.y;
                o.z = v.z;
                o.u = q3.z != 0.0f ? q3.x / q3.z : q3.x;
                o.v = q3.z != 0.0f ? q3.y / q3.z : q3.y;
                o.rgba = colour(q2.x * tint.x, q2.y * tint.y, q2.z * tint.z, a);
                o.fog = 1.0f;
                verts[k % 3u] = o;
                inside[k % 3u] = proj.inside(v);
                if (k == 0u)
                    first = o;
                if (k < 2u || !inside[0] || !inside[1] || !inside[2])
                    continue;
                switch (prim & 7u)
                {
                case 3:
                    if (k % 3u == 2u)
                        out.triangle(gs, prim, matrix, verts[(k - 2u) % 3u], verts[(k - 1u) % 3u], verts[k % 3u]);
                    break;
                case 5: out.triangle(gs, prim, matrix, first, verts[(k - 1u) % 3u], verts[k % 3u]); break;
                default: out.triangle(gs, prim, matrix, verts[(k - 2u) % 3u], verts[(k - 1u) % 3u], verts[k % 3u]); break;
                }
            }
        }

        // One MSCNT of programs 1 / 10: records until the one with EOP (a side record's EOP only skips a qword).
        void Skin::main(const std::vector<Qword> &in, Builder &out)
        {
            size_t at = 0;
            for (int guard = 0; guard < 512 && at < 0x100u; ++guard)
            {
                const Qword head = in[at & 1023u];
                ++at;
                const uint32_t count = head[0] & 0x7FFFu, type = head[3] & 0xFFFFu;
                const bool eop = (head[0] & 0x8000u) != 0u;
                if (type == 0u)
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const Qword &q = in[(at + i) & 1023u];
                        gs.set(q[2] & 0xFFu, q[0] | uint64_t(q[1]) << 32);
                    }
                    at += count;
                }
                else if (type < 4u)
                {
                    if (eop)
                        ++at;
                    continue;
                }
                else
                {
                    strip(in, at, count, head, out);
                    at += size_t(count) * 6u;
                }
                if (eop)
                    break;
            }
        }

        // Four XYZF2 two pixels either side of the edge across its narrower extent, at each end's depth.
        void Skin::quad(const Screen &a0, const Screen &b0, Builder &out) const
        {
            Screen a = a0, b = b0;
            const bool wide = std::fabs(a.y - b.y) < std::fabs(a.x - b.x);
            if (wide ? b.x < a.x : b.y < a.y)
                std::swap(a, b);
            const float dx = wide ? 0.0f : kOutlineHalfWidth, dy = wide ? kOutlineHalfWidth : 0.0f;
            const auto corner = [&](const Screen &s, float sign) {
                Vertex v;
                v.x = s.x + sign * dx;
                v.y = s.y + sign * dy;
                v.z = s.z;
                v.rgba = colour(0, 0, 0, 32.0f * s.alpha);
                v.fog = 1.0f;
                return v;
            };
            const Vertex v0 = corner(a, -1), v1 = corner(a, 1), v2 = corner(b, -1), v3 = corner(b, 1);
            GsRegs outline;
            out.triangle(outline, kOutlinePrim, kIdentity, v0, v1, v2);
            out.triangle(outline, kOutlinePrim, kIdentity, v1, v2, v3);
        }

        // Screen-space sign of the turn p0 -> p1 -> p2, as the programs' OPMSUB z: true when negative.
        bool turnsNegative(const Screen &p0, const Screen &p1, const Screen &p2)
        {
            const float e1x = p1.x - p0.x, e1y = p1.y - p0.y, e2x = p2.x - p1.x, e2y = p2.y - p1.y;
            return e1x * e2y - e1y * e2x < 0.0f;
        }

        // Programs 5 / 11 on one MSCNT: the strips' records (A+D blocks skipped, three-vertex strips skipped).
        void Skin::silhouette(const std::vector<Qword> &in, Builder &out) const
        {
            size_t at = 0;
            std::vector<Screen> s;
            for (int guard = 0; guard < 512 && at < 0x100u; ++guard)
            {
                const Qword head = in[at & 1023u];
                ++at;
                const uint32_t count = head[0] & 0x7FFFu, type = head[3] & 0xFFFFu;
                const bool eop = (head[0] & 0x8000u) != 0u;
                if (type == 0u)
                    at += count;
                else if (type < 4u)
                {
                    if (eop)
                        ++at;
                    continue;
                }
                else
                {
                    const uint32_t nbones = bones(type - 4u);
                    s.assign(count, Screen{});
                    for (uint32_t k = 0; count != 3u && k < count; ++k)
                    {
                        const size_t base = at + size_t(k) * 6u;
                        const Vec4 q0 = asVec(in[base & 1023u]);
                        const Mat4 m = blend(in[(base + 4u) & 1023u], asVec(in[(base + 5u) & 1023u]), nbones);
                        s[k] = screen(point(m, q0.x, q0.y, q0.z), perBoneAlpha() ? m[3].w : 1.0f);
                    }
                    for (uint32_t j = 0; count > 3u && j + 3u < count; ++j)
                    {
                        if (!s[j].inside || !s[j + 1].inside || !s[j + 2].inside || !s[j + 3].inside)
                            continue;
                        if (turnsNegative(s[j], s[j + 1], s[j + 2]) == turnsNegative(s[j + 1], s[j + 2], s[j + 3]))
                            quad(s[j + 1], s[j + 2], out);
                    }
                    at += size_t(count) * 6u;
                }
                if (eop)
                    break;
            }
        }

        // Programs 6 / 12 and 7 / 13 on one MSCNT: a header (count, bone type) then three-qword vertices (position,
        // bone offsets, weights), two per edge or four per (wing, A, B, wing) record.
        void Skin::edges(const std::vector<Qword> &in, Builder &out) const
        {
            const Qword head = in[0];
            const uint32_t count = head[0] & 0x7FFFu, nbones = bones(head[3] & 0xFFFFu);
            const bool wings = program == 7u || program == 13u;
            const uint32_t per = wings ? 4u : 2u;
            if (count * per * 3u + 1u > 0x100u)
                return;
            Screen s[4];
            for (uint32_t e = 0; e < count; ++e)
            {
                for (uint32_t k = 0; k < per; ++k)
                {
                    const size_t base = 1u + (size_t(e) * per + k) * 3u;
                    const Vec4 q0 = asVec(in[base]);
                    const Mat4 m = blend(in[base + 1u], asVec(in[base + 2u]), nbones);
                    s[k] = screen(point(m, q0.x, q0.y, q0.z), perBoneAlpha() ? m[3].w : 1.0f);
                }
                if (!wings)
                {
                    if (s[0].inside && s[1].inside)
                        quad(s[0], s[1], out);
                    continue;
                }
                if (s[0].inside && s[1].inside && s[2].inside && s[3].inside &&
                    turnsNegative(s[0], s[1], s[2]) == turnsNegative(s[1], s[2], s[3]))
                    quad(s[1], s[2], out);
            }
        }

        void skinned(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const uint32_t obj = getRegU32(ctx, 4), mesh = getRegU32(ctx, 5), list = getRegU32(ctx, 7);
            const int16_t index = static_cast<int16_t>(getRegU32(ctx, 8));
            const uint32_t pass = getRegU32(ctx, 9), skeleton = getRegU32(ctx, 10), program = getRegU32(ctx, 11);
            const float key = ctx->f[12];
            Mat4 current;
            for (int i = 0; i < 4; ++i)
            {
                const Vu0Vector &r = ctx->vu0_vf[4 + i];
                current[i] = {r.f[0], r.f[1], r.f[2], r.f[3]};
            }
            func_0x198b68(rdram, ctx, runtime);
            const Guest g = guest(rdram, runtime);
            const int32_t bank = residentBank(g, ctx);

            Skin s;
            s.program = program;
            const Mat4 h = mul(current, mat4(g, g.u32(obj + 4)));
            const uint32_t tint = g.u32(obj + 0x10);
            s.tint = {g.f32(tint), g.f32(tint + 4), g.f32(tint + 8), 0};
            s.alpha = (pass & 4u) ? 1.0f : g.f32(obj + 0x20);
            const uint32_t bonesAt = g.u32(obj + 0x14), mask = g.u32(obj + 0x2C), table = g.u32(obj + 0x34);
            const float boneAlpha = g.f32(obj + 0x30);
            const bool masked = program >= 10u && program <= 13u && table != 0u && mask != 0u;
            const int32_t wanted = g.s32(skeleton + 4);
            for (int32_t node = 0; static_cast<int32_t>(s.palette.size()) < wanted && node < 256; ++node)
            {
                const uint32_t at = skeleton + 0x10u + static_cast<uint32_t>(node) * 0x80u;
                if (g.s32(at + 0x78) < 0)
                    continue;
                Mat4 p = mul(mat4(g, at), mat4(g, bonesAt + static_cast<uint32_t>(node) * 0x40u));
                if (masked && ((static_cast<int32_t>(mask) >> g.at(table + static_cast<uint32_t>(node))[0]) & 1))
                    p[3].w = boneAlpha;
                s.palette.push_back(mul(p, h));
            }
            s.proj = projection(g, kScale, kOffset, kGuard);
            s.proj.matrix(s.matrix);

            Draw draw;
            draw.list = listOf(list);
            draw.key = (pass & 4u) ? 0.0f : key;
            draw.index = index;
            Builder builder(draw, fogColour(g), bank);
            walkMesh(g, mesh, [&](const std::vector<Qword> &in) {
                switch (program)
                {
                case 1: case 10: s.main(in, builder); break;
                case 5: case 11: s.silhouette(in, builder); break;
                case 6: case 12: case 7: case 13: s.edges(in, builder); break;
                default: break;
                }
            });
            queue(std::move(draw));
        }
    }

    void installSkinned(bozo::port::Registry &runtime) { runtime.registerFunction(kSkinned, &skinned); }
}
