// FFOP's rigid meshes natively: VU1 programs 0, 8 and 9 (blobs 0x1EDD60, 0x1FE3B0, 0x2015A0) redone at the EE call that
// queues them, 0x198140(ctx, mesh, packet, list, index, pass, program, key). The game's own call still builds the packet
// (the frame links it); the mesh's batches are then read as the program reads them: A+D blocks (w 0), strips of
// three-qword vertices (w 4), bounding spheres (w 6) and material headers (any other w), lit by three directional lights
// plus ambient or prelit by the object's tint, fogged by depth, back faces culled by the strip's parity.
#include "ffop_port.h"

#include "ffop_3d.h"
#include "ffop_functions.h"

#include <algorithm>
#include <cmath>

namespace ffop::draw3d
{
    namespace
    {
        constexpr uint32_t kRigid = 0x00198140u;

        constexpr uint32_t kGuard = 0x002B3690u;
        constexpr uint32_t kScale = 0x002B36A0u;
        constexpr uint32_t kOffset = 0x002B36B0u;

        struct Rigid
        {
            Mat4 m;
            Vec4 normal[3];
            Vec4 light[3];
            Vec4 ambient, tint;
            float alpha = 1, fogOffset = 0, fogScale = 0, near = 0, scrollU = 0, scrollV = 0;
            int32_t flip = 0;
            bool clipper = false;
            Projection proj;
            float matrix[16] = {};

            bool lit = true;
            Vec4 materialAmbient;
            GsRegs gs;

            void strip(const std::vector<Qword> &in, size_t at, uint32_t count, const Qword &head, Builder &out);
            void batch(const std::vector<Qword> &in, Builder &out);
        };

        Vec4 asVec(const Qword &q) { return {asFloat(q[0]), asFloat(q[1]), asFloat(q[2]), asFloat(q[3])}; }

        void Rigid::strip(const std::vector<Qword> &in, size_t at, uint32_t count, const Qword &head, Builder &out)
        {
            const uint32_t prim = (head[1] >> 15) & 0x7FFu, mode = head[1] & 6u;
            const bool twoSided = mode == 4u;
            const bool initial = static_cast<int32_t>(mode) + flip == 0;
            const float facing = proj.px * proj.py < 0.0f ? 1.0f : -1.0f;
            Vec4 view[3];
            Vertex verts[3];
            Vertex first;
            for (uint32_t k = 0; k < count; ++k)
            {
                const Vec4 q0 = asVec(in[(at + k * 3u) & 1023u]), q1 = asVec(in[(at + k * 3u + 1u) & 1023u]);
                const Vec4 q2 = asVec(in[(at + k * 3u + 2u) & 1023u]);
                const Vec4 v = point(m, q0.x, q0.y, q0.z);
                float r, g, b;
                if (lit)
                {
                    const float dx = std::max(0.0f, q1.x * normal[0].x + q1.y * normal[1].x + q1.z * normal[2].x);
                    const float dy = std::max(0.0f, q1.x * normal[0].y + q1.y * normal[1].y + q1.z * normal[2].y);
                    const float dz = std::max(0.0f, q1.x * normal[0].z + q1.y * normal[1].z + q1.z * normal[2].z);
                    r = std::min(255.0f, materialAmbient.x + q2.x * (dx * light[0].x + dy * light[1].x + dz * light[2].x));
                    g = std::min(255.0f, materialAmbient.y + q2.y * (dx * light[0].y + dy * light[1].y + dz * light[2].y));
                    b = std::min(255.0f, materialAmbient.z + q2.z * (dx * light[0].z + dy * light[1].z + dz * light[2].z));
                }
                else
                {
                    r = q2.x * tint.x;
                    g = q2.y * tint.y;
                    b = q2.z * tint.z;
                }
                Vertex o;
                o.x = v.x;
                o.y = v.y;
                o.z = v.z;
                o.u = q0.w + scrollU;
                o.v = q1.w + scrollV;
                o.rgba = colour(r, g, b, q2.w * alpha);
                o.fog = std::floor(std::clamp(v.z * fogScale + fogOffset, 0.0f, 255.0f)) / 255.0f;
                view[k % 3u] = v;
                verts[k % 3u] = o;
                if (k == 0u)
                    first = o;
                if (k < 2u)
                    continue;
                const Vec4 &a = view[(k - 2u) % 3u], &bb = view[(k - 1u) % 3u], &c = view[k % 3u];
                if (!clipper && (!proj.inside(a) || !proj.inside(bb) || !proj.inside(c)))
                    continue;
                if (!twoSided)
                {
                    const float det = a.x * (bb.y * c.z - bb.z * c.y) - a.y * (bb.x * c.z - bb.z * c.x) + a.z * (bb.x * c.y - bb.y * c.x);
                    const bool negative = facing * det < 0.0f;
                    const bool parity = initial != ((k & 1u) != 0u);
                    if (negative != parity)
                        continue;
                }
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

        // One MSCNT: the records from TOP until the one carrying EOP (a material header's EOP only says its colour
        // follows).
        void Rigid::batch(const std::vector<Qword> &in, Builder &out)
        {
            size_t at = 0;
            for (int guard = 0; guard < 512 && at < 0x190u; ++guard)
            {
                const Qword head = in[at & 1023u];
                ++at;
                const int16_t x = static_cast<int16_t>(head[0] & 0xFFFFu);
                const uint32_t count = head[0] & 0x7FFFu, type = head[3] & 0xFFFFu;
                const bool eop = x < 0;
                if (type == 0u)
                {
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const Qword &q = in[(at + i) & 1023u];
                        gs.set(q[2] & 0xFFu, q[0] | uint64_t(q[1]) << 32);
                    }
                    at += count;
                }
                else if (type == 4u)
                {
                    strip(in, at, count, head, out);
                    at += size_t(count) * 3u;
                }
                else if (type == 6u)
                {
                    const Vec4 s = asVec(in[at & 1023u]);
                    ++at;
                    const Vec4 c = point(m, s.x, s.y, s.z);
                    if (c.z > s.w - near)
                        at += count;
                }
                else
                {
                    if (!eop)
                    {
                        if (lit)
                            materialAmbient = {};
                        continue;
                    }
                    const Vec4 c = asVec(in[at & 1023u]);
                    ++at;
                    materialAmbient = {ambient.x * c.x, ambient.y * c.y, ambient.z * c.z, 0};
                    lit = (count & 1u) == 0u;
                    continue;
                }
                if (eop)
                    break;
            }
        }

        void rigid(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const uint32_t obj = getRegU32(ctx, 4), mesh = getRegU32(ctx, 5), list = getRegU32(ctx, 7);
            const int16_t index = static_cast<int16_t>(getRegU32(ctx, 8));
            const uint32_t pass = getRegU32(ctx, 9), program = getRegU32(ctx, 10);
            const float key = ctx->f[12];
            func_0x198140(rdram, ctx, runtime);
            const Guest g = guest(rdram, runtime);
            const int32_t bank = residentBank(g, ctx);

            Rigid r;
            const Mat4 local = mat4(g, g.u32(obj)), camera = mat4(g, g.u32(obj + 4));
            r.m = mul(local, camera);
            const uint32_t dirs = g.u32(obj + 8), cols = g.u32(obj + 0xC), tint = g.u32(obj + 0x10);
            const Vec4 l[3] = {vec4(g, dirs), vec4(g, dirs + 16), vec4(g, dirs + 32)};
            for (int i = 0; i < 3; ++i)
            {
                const Vec4 &a = local[i];
                r.normal[i] = {a.x * l[0].x + a.y * l[1].x + a.z * l[2].x, a.x * l[0].y + a.y * l[1].y + a.z * l[2].y,
                               a.x * l[0].z + a.y * l[1].z + a.z * l[2].z, 0};
                r.light[i] = vec4(g, cols + 16u * i);
            }
            r.ambient = {g.f32(cols + 48), g.f32(cols + 52), g.f32(cols + 56), 0};
            r.alpha = (pass & 4u) ? 1.0f : g.f32(obj + 0x20);
            r.tint = {g.f32(tint), g.f32(tint + 4), g.f32(tint + 8), 0};
            const uint32_t flags = g.u32(obj + 0x18);
            r.flip = (flags & 0x200u) ? -2 : 0;
            r.clipper = (flags & 1u) != 0u;
            const Vec4 fog = draw3d::fog(g, key, (pass & 4u) != 0u);
            r.fogOffset = fog.y;
            r.fogScale = fog.z;
            r.near = fog.w;
            if (program == 8u || program == 9u)
            {
                r.scrollU = g.f32(obj + 0x24);
                r.scrollV = g.f32(obj + 0x28);
            }
            r.proj = projection(g, kScale, kOffset, kGuard);
            r.proj.matrix(r.matrix);

            Draw draw;
            draw.list = listOf(list);
            draw.key = (pass & 4u) ? 0.0f : key;
            draw.index = index;
            Builder builder(draw, fogColour(g), bank);
            walkMesh(g, mesh, [&](const std::vector<Qword> &in) { r.batch(in, builder); });
            queue(std::move(draw));
        }
    }

    void installRigid(bozo::port::Registry &runtime) { runtime.registerFunction(kRigid, &rigid); }
}
