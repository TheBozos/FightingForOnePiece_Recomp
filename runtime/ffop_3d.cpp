// FFOP's three draw lists natively: the meshes the hooks turned into triangles, kept per list with the game's sort key
// and drawn when 0x199EE0 links the lists into the frame (A opaque sorted, B the z-only pre-pass, C translucent
// sorted), so the 2D the frame splices after them lands on top as it does on the GS.
#include "ffop_port.h"

#include "ffop_3d.h"
#include "ffop_functions.h"

#include <algorithm>
#include <cmath>

namespace ffop::draw3d
{
    namespace
    {
        constexpr uint32_t kLink = 0x00199EE0u;

        constexpr uint32_t kListOpaque = 0x003545C0u;
        constexpr uint32_t kListDepthOnly = 0x003595D8u;
        constexpr uint32_t kFogColour = 0x00363608u;
        constexpr uint32_t kFogMode = 0x00363610u;
        constexpr uint32_t kFogScale = 0x00363624u;
        constexpr uint32_t kFogOffset = 0x00363628u;
        constexpr uint32_t kFogMax = 0x0052B5DCu;
        constexpr uint32_t kFogBlock = 0x002B3680u;
        constexpr int32_t kZAlwaysGp = -0x28B8;
        constexpr int32_t kResidentBankGp = -0x16FC;
        constexpr uint8_t kAlphaRef = 0x70;

        std::vector<Draw> g_lists[3];

        Material material(const GsRegs &gs, uint32_t prim, uint32_t fogColour, bool textured)
        {
            Material m;
            m.textured = textured;
            m.directColour = !textured;
            const uint32_t tfx = static_cast<uint32_t>((gs.tex0 >> 35) & 3u);
            m.textureFunction = static_cast<Material::TextureFunction>(tfx);
            m.textureAlpha = ((gs.tex0 >> 34) & 1u) != 0u;
            m.nearest = ((gs.tex1 >> 5) & 1u) == 0u;
            m.mipmap = bozo::render::mipmapOf(gs.tex1);
            const uint32_t tw = 1u << ((gs.tex0 >> 26) & 0xFu), th = 1u << ((gs.tex0 >> 30) & 0xFu);
            const uint32_t wms = static_cast<uint32_t>(gs.clamp & 3u), wmt = static_cast<uint32_t>((gs.clamp >> 2) & 3u);
            m.clampU = wms == 1u || wms == 2u;
            m.clampV = wmt == 1u || wmt == 2u;
            if (wms == 2u || wmt == 2u)
            {
                const auto field = [&](uint32_t shift) { return static_cast<float>((gs.clamp >> shift) & 0x3FFu); };
                m.uvBounds[0] = wms == 2u ? field(4) / tw : 0.0f;
                m.uvBounds[2] = wms == 2u ? (field(14) + 1.0f) / tw : 1.0f;
                m.uvBounds[1] = wmt == 2u ? field(24) / th : 0.0f;
                m.uvBounds[3] = wmt == 2u ? (field(34) + 1.0f) / th : 1.0f;
            }
            if (wms == 3u || wmt == 3u)
                m.region = gs.clamp;
            if ((prim >> 6) & 1u)
            {
                m.alphaBlend = true;
                m.gsBlend = true;
                m.blend = Material::Blend::Alpha;
                m.blendA = static_cast<uint8_t>(gs.alpha & 3u);
                m.blendB = static_cast<uint8_t>((gs.alpha >> 2) & 3u);
                m.blendC = static_cast<uint8_t>((gs.alpha >> 4) & 3u);
                m.blendD = static_cast<uint8_t>((gs.alpha >> 6) & 3u);
                m.blendFix = static_cast<uint8_t>(gs.alpha >> 32);
            }
            else
            {
                m.alphaBlend = false;
                m.blend = Material::Blend::Opaque;
            }
            if ((prim >> 5) & 1u)
            {
                m.fog = true;
                m.fogColour = fogColour & 0xFFFFFFu;
            }
            m.depthTest = true;
            m.depthWrite = true;
            return m;
        }

        bool before(const Draw &a, const Draw &b)
        {
            if (a.key != b.key)
                return a.key < b.key;
            return a.index < b.index;
        }

        void draw(bozo::render::Renderer &renderer, const Batch &b, const Material &m)
        {
            if (b.texture)
                renderer.draw(m, b.matrix, b.vertices.data(), static_cast<uint32_t>(b.vertices.size()), b.texture);
            else
                renderer.draw(m, b.matrix, b.vertices.data(), static_cast<uint32_t>(b.vertices.size()));
        }

        // TEST_1 as 0x199EE0 sets it for the lists: ATE GEQUAL 0x70 with AFAIL FB_ONLY (the texels under the reference
        // drawn without their depth), ZTE GEQUAL; ZTST ALWAYS and reference 0 while the game asks for it.
        void submit(bool zAlways)
        {
            bozo::render::Renderer &renderer = bozo::render::Renderer::instance();
            std::stable_sort(g_lists[0].begin(), g_lists[0].end(), before);
            std::stable_sort(g_lists[2].begin(), g_lists[2].end(), before);
            const uint8_t ref = zAlways ? 0u : kAlphaRef;
            for (int list : {0, 1, 2})
            {
                for (const Draw &d : g_lists[list])
                    for (const Batch &b : d.batches)
                    {
                        if (b.vertices.empty())
                            continue;
                        Material m = b.material;
                        if (zAlways)
                            m.depthTest = false;
                        if (d.list == List::DepthOnly)
                            m.colourMask = 0;
                        if (b.minAlpha >= ref)
                        {
                            draw(renderer, b, m);
                            continue;
                        }
                        m.alphaTest = true;
                        m.alphaFunc = Material::AlphaFunc::GreaterEqual;
                        m.alphaRef = ref;
                        draw(renderer, b, m);
                        if (d.list == List::DepthOnly)
                            continue;
                        m.alphaFunc = Material::AlphaFunc::Less;
                        m.depthWrite = false;
                        draw(renderer, b, m);
                    }
                g_lists[list].clear();
            }
        }

        // 0x199EE0: the lists linked into the frame (and emptied).
        void link(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const Guest g = guest(rdram, runtime);
            const bool zAlways = g.u32(getRegU32(ctx, 28) + static_cast<uint32_t>(kZAlwaysGp)) != 0u;
            func_0x199ee0(rdram, ctx, runtime);
            submit(zAlways);
        }
    }

    Guest guest(uint8_t *rdram, Runtime *runtime) { return Guest{rdram, bozo::port::scratchpad(runtime)}; }

    Vec4 vec4(const Guest &g, uint32_t address)
    {
        return {g.f32(address), g.f32(address + 4), g.f32(address + 8), g.f32(address + 12)};
    }

    Mat4 mat4(const Guest &g, uint32_t address)
    {
        return {vec4(g, address), vec4(g, address + 16), vec4(g, address + 32), vec4(g, address + 48)};
    }

    Mat4 mul(const Mat4 &a, const Mat4 &b)
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
        {
            const Vec4 &s = a[i];
            r[i] = {s.x * b[0].x + s.y * b[1].x + s.z * b[2].x + s.w * b[3].x, s.x * b[0].y + s.y * b[1].y + s.z * b[2].y + s.w * b[3].y,
                    s.x * b[0].z + s.y * b[1].z + s.z * b[2].z + s.w * b[3].z, s.x * b[0].w + s.y * b[1].w + s.z * b[2].w + s.w * b[3].w};
        }
        return r;
    }

    Vec4 point(const Mat4 &m, float x, float y, float z)
    {
        return {x * m[0].x + y * m[1].x + z * m[2].x + m[3].x, x * m[0].y + y * m[1].y + z * m[2].y + m[3].y,
                x * m[0].z + y * m[1].z + z * m[2].z + m[3].z, x * m[0].w + y * m[1].w + z * m[2].w + m[3].w};
    }

    void Projection::matrix(float out[16]) const
    {
        const float m[16] = {-px, 0, 0, 0, 0, -py, 0, 0, -ox, -oy, -oz, -1, 0, 0, -pz, 0};
        std::copy(m, m + 16, out);
    }

    bool Projection::inside(const Vec4 &v) const
    {
        const float q = 1.0f / v.z;
        const float cx = ((px * v.x + ox * v.z) * q - 2048.0f) * guard.x;
        const float cy = ((py * v.y + oy * v.z) * q - 2048.0f) * guard.y;
        const float cz = guard.z - v.z;
        const float w = std::fabs(guard.w);
        return std::fabs(cx) <= w && std::fabs(cy) <= w && std::fabs(cz) <= w;
    }

    Projection projection(const Guest &g, uint32_t scale, uint32_t offset, uint32_t guard)
    {
        Projection p;
        p.px = g.f32(scale);
        p.py = g.f32(scale + 4);
        p.pz = g.f32(scale + 8);
        p.ox = g.f32(offset);
        p.oy = g.f32(offset + 4);
        p.oz = g.f32(offset + 8);
        p.guard = vec4(g, guard);
        return p;
    }

    void GsRegs::set(uint32_t reg, uint64_t value)
    {
        switch (reg)
        {
        case 0x06: tex0 = value; break;
        case 0x08: clamp = value; break;
        case 0x14: tex1 = value; break;
        case 0x42: alpha = value; break;
        default: break;
        }
    }

    uint32_t colour(float r, float g, float b, float a)
    {
        const auto c = [](float f) {
            const int i = static_cast<int>(f);
            return static_cast<uint32_t>(i < 0 ? 0 : i > 255 ? 255 : i);
        };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }

    void Builder::triangle(const GsRegs &gs, uint32_t prim, const float matrix[16], const Vertex &a, const Vertex &b,
                           const Vertex &c)
    {
        const bool same = m_batch && m_prim == prim && m_gs.tex0 == gs.tex0 && m_gs.tex1 == gs.tex1 && m_gs.clamp == gs.clamp &&
                          m_gs.alpha == gs.alpha && (m_matrix == matrix || std::equal(matrix, matrix + 16, m_batch->matrix));
        if (!same)
        {
            Batch batch;
            const bool tme = (prim >> 4) & 1u;
            m_textureAlpha = 0x80;
            if (tme)
                batch.texture = texture(gs.tex0, m_bank, m_textureAlpha);
            if (!((gs.tex0 >> 34) & 1u))
                m_textureAlpha = 0x80;
            batch.material = material(gs, prim, m_fogColour, tme && batch.texture);
            std::copy(matrix, matrix + 16, batch.matrix);
            batch.minAlpha = 0xFF;
            m_draw.batches.push_back(std::move(batch));
            m_batch = &m_draw.batches.back();
            m_gs = gs;
            m_prim = prim;
            m_matrix = matrix;
        }
        for (const Vertex *v : {&a, &b, &c})
        {
            const uint32_t alpha = (v->rgba >> 24) * m_textureAlpha >> 7;
            m_batch->minAlpha = static_cast<uint8_t>(std::min<uint32_t>(m_batch->minAlpha, std::min<uint32_t>(alpha, 0xFFu)));
            m_batch->vertices.push_back(*v);
        }
    }

    List listOf(uint32_t base)
    {
        if (base == kListOpaque)
            return List::Opaque;
        if (base == kListDepthOnly)
            return List::DepthOnly;
        return List::Translucent;
    }

    void queue(Draw &&draw)
    {
        if (!draw.batches.empty())
            g_lists[static_cast<int>(draw.list)].push_back(std::move(draw));
    }

    uint32_t fogColour(const Guest &g) { return g.u32(kFogColour); }

    Vec4 fog(const Guest &g, float key, bool none)
    {
        Vec4 f = vec4(g, kFogBlock);
        if (none)
            return f;
        const int32_t mode = g.s32(kFogMode);
        if (mode < 0)
            f.y = static_cast<float>(mode + 255), f.z = 0.0f;
        else if (mode == 0)
            f.y = g.f32(kFogMax), f.z = 0.0f;
        else if (mode == 1)
            f.y = std::max(0.0f, std::min(g.f32(kFogMax), key * g.f32(kFogScale) + g.f32(kFogOffset))), f.z = 0.0f;
        else if (mode == 2)
            f.y = g.f32(kFogOffset), f.z = g.f32(kFogScale);
        else if (mode == 3)
            f.y = 0.0f, f.z = 0.0f;
        return f;
    }

    int32_t residentBank(const Guest &g, R5900Context *ctx)
    {
        return g.s32(getRegU32(ctx, 28) + static_cast<uint32_t>(kResidentBankGp));
    }

    bool walkMeshPacket(const Guest &g, uint32_t packet, std::vector<Qword> &buffer, void (*kick)(void *, const std::vector<Qword> &),
                        void *user)
    {
        std::vector<uint32_t> words;
        uint32_t tag = packet;
        for (int tags = 0; tags < 64; ++tags)
        {
            const uint32_t head = g.u32(tag);
            const uint32_t qwc = head & 0xFFFFu, id = (head >> 28) & 7u;
            if (id != 1u && id != 6u && id != 7u)
                return false;
            words.push_back(g.u32(tag + 8));
            words.push_back(g.u32(tag + 12));
            const size_t at = words.size();
            words.resize(at + size_t(qwc) * 4u);
            std::memcpy(&words[at], g.at(tag + 16), size_t(qwc) * 16u);
            if (id != 1u)
                break;
            tag += 16u + qwc * 16u;
        }
        uint32_t cl = 1, wl = 1;
        for (size_t i = 0; i < words.size();)
        {
            const uint32_t w = words[i++];
            const uint32_t cmd = (w >> 24) & 0x7Fu, imm = w & 0xFFFFu, num = (w >> 16) & 0xFFu;
            if ((cmd & 0x60u) == 0x60u)
            {
                const uint32_t vn = (cmd >> 2) & 3u, vl = cmd & 3u, count = num ? num : 256u;
                const uint32_t elements = vn + 1u, bits = 32u >> vl;
                const size_t length = (size_t(count) * elements * bits + 31u) / 32u;
                if (i + length > words.size() || vl == 3u)
                    return false;
                const uint8_t *src = reinterpret_cast<const uint8_t *>(&words[i]);
                const bool sign = !((imm >> 14) & 1u);
                const uint32_t base = imm & 0x3FFu;
                for (uint32_t k = 0; k < count; ++k)
                {
                    const uint32_t step = wl <= cl ? (k / wl) * cl + k % wl : k;
                    Qword &q = buffer[(base + step) & 1023u];
                    for (uint32_t e = 0; e < elements; ++e)
                    {
                        const size_t n = size_t(k) * elements + e;
                        uint32_t v = 0;
                        if (bits == 32u)
                            std::memcpy(&v, src + n * 4u, 4);
                        else if (bits == 16u)
                        {
                            uint16_t h;
                            std::memcpy(&h, src + n * 2u, 2);
                            v = sign ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(h))) : h;
                        }
                        else
                            v = sign ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(src[n]))) : src[n];
                        q[e] = v;
                    }
                }
                i += length;
                continue;
            }
            switch (cmd)
            {
            case 0x01:
                cl = imm & 0xFFu;
                wl = (imm >> 8) & 0xFFu;
                if (cl == 0u)
                    cl = 1u;
                if (wl == 0u)
                    wl = 1u;
                break;
            case 0x00: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            case 0x10: case 0x11: case 0x13:
                break;
            case 0x14: case 0x15: case 0x17:
                kick(user, buffer);
                break;
            case 0x20: i += 1; break;
            case 0x30: case 0x31: i += 4; break;
            case 0x4A: i += size_t(num ? num : 256u) * 2u; break;
            case 0x50: case 0x51: i += size_t(imm ? imm : 65536u) * 4u; break;
            default: return false;
            }
        }
        return true;
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.registerFunction(kLink, &link);
        installTextures(runtime);
        installRigid(runtime);
        installSkinned(runtime);
    }
}
