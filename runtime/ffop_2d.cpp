#include "ffop_port.h"

#include "ffop_functions.h"
#include "ffop_gs_page.h"

#include "bozo/context.h"
#include "bozo/hash.h"
#include "bozo/render/renderer.h"
#include "bozo/render/texture_pack.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ffop::draw2d
{
    namespace
    {
        using bozo::render::Material;
        using bozo::render::Texture;
        using bozo::render::Vertex;
        using TexturePtr = std::shared_ptr<const Texture>;
        namespace packs = bozo::render::packs;

        constexpr uint32_t kSprite = 0x0019F940u;
        constexpr uint32_t kRotatedSprite = 0x001A1350u;
        constexpr uint32_t kFill = 0x001A1FF0u;
        constexpr uint32_t kTriangleBegin = 0x001A2ED8u;
        constexpr uint32_t kTriangle = 0x001A31E0u;
        constexpr uint32_t kTriangleEnd = 0x001A3600u;
        constexpr uint32_t kSpriteBatchBegin = 0x001A3CE8u;
        constexpr uint32_t kSpriteBatchDraw = 0x001A3D20u;
        constexpr uint32_t kSpriteBatchEnd = 0x001A3F80u;
        constexpr uint32_t kSplice = 0x0019A510u;

        constexpr int32_t kCentre = 0x8000;
        constexpr uint64_t kTexa = 0x80u;
        constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

        struct Tile
        {
            uint32_t width, height;
        };
        constexpr Tile kTiles[5] = {{64, 32}, {64, 32}, {64, 64}, {128, 64}, {128, 128}};

        struct Guest
        {
            const uint8_t *ram;
            const uint8_t *at(uint32_t address) const { return ram + (address & 0x01FFFFFFu); }
            uint32_t u32(uint32_t address) const
            {
                uint32_t v;
                std::memcpy(&v, at(address), 4);
                return v;
            }
            int32_t s32(uint32_t address) const { return static_cast<int32_t>(u32(address)); }
            uint16_t u16(uint32_t address) const
            {
                uint16_t v;
                std::memcpy(&v, at(address), 2);
                return v;
            }
            int16_t s16(uint32_t address) const { return static_cast<int16_t>(u16(address)); }
            uint64_t u64(uint32_t address) const
            {
                uint64_t v;
                std::memcpy(&v, at(address), 8);
                return v;
            }
            float f32(uint32_t address) const
            {
                float v;
                std::memcpy(&v, at(address), 4);
                return v;
            }
        };

        struct Image
        {
            uint32_t pixels, clut, psm, cpsm;
            int32_t width, height;
        };

        Image image(const Guest &g, uint32_t descr)
        {
            return {g.u32(descr), g.u32(descr + 4), g.u16(descr + 8), g.u16(descr + 0xA), g.s16(descr + 0xC), g.s16(descr + 0xE)};
        }

        bool knownFormat(uint32_t psm) { return psm == 0x00 || psm == 0x01 || psm == 0x02 || psm == 0x13 || psm == 0x14; }

        uint32_t tileBytes(uint32_t psm, uint32_t tw, uint32_t th)
        {
            switch (psm)
            {
            case 0x00: return (tw * th * 8u >> 5) * 16u;
            case 0x01: return (tw * th * 6u >> 5) * 16u;
            case 0x02: return ((th + 7u) & ~7u) * (((tw + 15u) & ~15u) / 2u) * 4u;
            case 0x13: return (((tw + 15u) & ~15u) / 2u) * (((th + 15u) & ~15u) / 2u) * 4u;
            default: return (((th + 15u) & ~15u) / 2u) * (((tw + 31u) & ~31u) / 4u) * 4u;
            }
        }

        // The texels of one tile as the GS reads them after the game's upload: the tile's bytes are a PSMCT32 rectangle
        // written at BP 0, BW 1, read back in the image's own format.
        void decodeTile(const uint8_t *src, uint32_t psm, uint32_t tw, uint32_t th, uint32_t ox, uint32_t oy,
                        packs::TextureRecord &out)
        {
            const uint32_t stride = out.width;
            if (psm == 0x00 || psm == 0x01)
            {
                const uint32_t bpp = psm == 0x00 ? 4u : 3u;
                for (uint32_t y = 0; y < th; ++y)
                    for (uint32_t x = 0; x < tw; ++x)
                    {
                        uint8_t *d = &out.texels[((oy + y) * stride + ox + x) * 4u];
                        const uint8_t *s = src + (y * tw + x) * bpp;
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                        d[3] = bpp == 4u ? s[3] : 0u;
                    }
                return;
            }
            std::array<uint8_t, gs::kPageBytes> page{};
            uint32_t w = 0, h = 0;
            if (psm == 0x02)
                w = (th + 7u) & ~7u, h = ((tw + 15u) & ~15u) / 2u;
            else if (psm == 0x13)
                w = ((tw + 15u) & ~15u) / 2u, h = ((th + 15u) & ~15u) / 2u;
            else
                w = ((th + 15u) & ~15u) / 2u, h = ((tw + 31u) & ~31u) / 4u;
            for (uint32_t y = 0; y < h && y < 32u; ++y)
                for (uint32_t x = 0; x < w && x < 64u; ++x)
                    std::memcpy(&page[gs::ct32(x, y)], src + (y * w + x) * 4u, 4);
            for (uint32_t y = 0; y < th; ++y)
                for (uint32_t x = 0; x < tw; ++x)
                {
                    const size_t at = (oy + y) * stride + ox + x;
                    if (psm == 0x02)
                        std::memcpy(&out.texels[at * 2u], &page[gs::ct16(x, y)], 2);
                    else if (psm == 0x13)
                        out.texels[at] = page[gs::t8(x, y)];
                    else
                    {
                        const uint32_t nibble = gs::t4(x, y);
                        out.texels[at] = (page[nibble >> 1] >> ((nibble & 1u) * 4u)) & 0xFu;
                    }
                }
        }

        void palette(const Guest &g, const Image &im, uint32_t out[256])
        {
            const bool eight = im.psm == 0x13;
            const uint32_t entries = eight ? 256u : 16u;
            for (uint32_t i = 0; i < entries; ++i)
            {
                const uint32_t at = eight ? ((i & 0xE7u) | ((i & 0x08u) << 1) | ((i & 0x10u) >> 1)) : i;
                out[i] = im.cpsm == 0x02 ? packs::ct16ToRgba(g.u16(im.clut + at * 2u), kTexa) : g.u32(im.clut + at * 4u);
            }
        }

        struct Cached
        {
            uint64_t hash = 0;
            TexturePtr texture;
        };
        std::unordered_map<uint64_t, Cached> g_textures;

        // The whole image (every tile when `firstTileOnly` is false) as one texture, cached by its bytes.
        TexturePtr texture(const Guest &g, const Image &im, bool firstTileOnly = false)
        {
            if (!knownFormat(im.psm) || im.width <= 0 || im.height <= 0 || im.width > 2048 || im.height > 2048)
                return nullptr;
            const Tile tile = kTiles[im.psm & 7u];
            const uint32_t width = firstTileOnly ? std::min<uint32_t>(im.width, tile.width) : static_cast<uint32_t>(im.width);
            const uint32_t height = firstTileOnly ? std::min<uint32_t>(im.height, tile.height) : static_cast<uint32_t>(im.height);
            uint32_t bytes = 0;
            for (uint32_t ty = 0; ty < height; ty += tile.height)
                for (uint32_t tx = 0; tx < width; tx += tile.width)
                    bytes += tileBytes(im.psm, std::min(width - tx, tile.width), std::min(height - ty, tile.height));
            uint64_t hash = bozo::hash::xxh64(g.at(im.pixels), bytes, im.psm | (uint64_t(width) << 16) | (uint64_t(height) << 32));
            const bool indexed = im.psm == 0x13 || im.psm == 0x14;
            if (indexed)
                hash = bozo::hash::xxh64(g.at(im.clut), (im.psm == 0x13 ? 256u : 16u) * (im.cpsm == 0x02 ? 2u : 4u), hash);
            const uint64_t key = (uint64_t(im.pixels) << 32) | (uint64_t(firstTileOnly) << 31) | im.clut;
            if (auto it = g_textures.find(key); it != g_textures.end() && it->second.hash == hash)
                return it->second.texture;
            packs::TextureRecord record;
            record.psm = im.psm == 0x01 ? 0x00u : im.psm;
            record.width = width;
            record.height = height;
            record.texels.assign(size_t(width) * height * (indexed ? 1u : im.psm == 0x02 ? 2u : 4u), 0);
            uint32_t at = im.pixels;
            for (uint32_t ty = 0; ty < height; ty += tile.height)
                for (uint32_t tx = 0; tx < width; tx += tile.width)
                {
                    const uint32_t tw = std::min(width - tx, tile.width), th = std::min(height - ty, tile.height);
                    decodeTile(g.at(at), im.psm, tw, th, tx, ty, record);
                    at += tileBytes(im.psm, tw, th);
                }
            if (im.psm == 0x01)
                for (size_t i = 3; i < record.texels.size(); i += 4)
                    record.texels[i] = static_cast<uint8_t>(kTexa);
            uint32_t colours[256] = {};
            if (indexed)
                palette(g, im, colours);
            auto decoded = std::make_shared<Texture>();
            packs::decodeRecord(*decoded, width, height, record.psm, record, colours, kTexa);
            decoded->key = hash | 1u;
            decoded->version = hash;
            if (g_textures.size() > 1024)
                g_textures.clear();
            g_textures[key] = {hash, decoded};
            return decoded;
        }

        struct Pending
        {
            Material material;
            std::vector<Vertex> vertices;
            TexturePtr texture;
        };
        std::vector<Pending> g_pending;
        bool g_started = false;

        bozo::render::Screen screen()
        {
            bozo::render::Screen s;
            s.originX = 2048.0f - 320.0f - 0.5f;
            s.originY = 2048.0f - 224.0f - 0.5f;
            s.width = 640.0f;
            s.height = 448.0f;
            return s;
        }

        // The draw state the game's params carry: ALPHA (+0x38), TEST (+0x40), TEX1 (+0x48) and the fog flash (+0x34).
        Material material(const Guest &g, uint32_t params, bool textured, bool withTex1)
        {
            Material m;
            m.textured = textured;
            m.textureFunction = Material::TextureFunction::Modulate;
            m.textureAlpha = true;
            const uint64_t alpha = g.u64(params + 0x38);
            m.alphaBlend = true;
            m.blend = Material::Blend::Alpha;
            m.gsBlend = true;
            m.blendA = static_cast<uint8_t>(alpha & 3u);
            m.blendB = static_cast<uint8_t>((alpha >> 2) & 3u);
            m.blendC = static_cast<uint8_t>((alpha >> 4) & 3u);
            m.blendD = static_cast<uint8_t>((alpha >> 6) & 3u);
            m.blendFix = static_cast<uint8_t>(alpha >> 32);
            const uint64_t test = g.u64(params + 0x40);
            const bool zte = (test >> 16) & 1u;
            const uint32_t ztst = static_cast<uint32_t>((test >> 17) & 3u);
            m.depthTest = zte && ztst >= 2u;
            m.depthGreater = ztst == 3u;
            m.depthWrite = zte && ztst != 0u;
            if (zte && ztst == 1u)
                m.depthTest = false;
            m.nearest = withTex1 ? ((g.u64(params + 0x48) >> 5) & 1u) == 0u : false;
            m.clampU = m.clampV = true;
            const uint8_t fog = g.at(params + 0x37)[0];
            if (fog != 0xFFu)
            {
                m.fog = true;
                const uint8_t *c = g.at(params + 0x34);
                m.fogColour = c[0] | (uint32_t(c[1]) << 8) | (uint32_t(c[2]) << 16);
            }
            return m;
        }

        float fogOf(const Guest &g, uint32_t params)
        {
            const uint8_t fog = g.at(params + 0x37)[0];
            return fog == 0xFFu ? 1.0f : static_cast<float>(fog) / 255.0f;
        }

        Vertex vertex(float x, float y, float z, float u, float v, uint32_t rgba, float fog)
        {
            Vertex out;
            out.x = x;
            out.y = y;
            out.z = z;
            out.u = u;
            out.v = v;
            out.rgba = rgba;
            out.fog = fog;
            return out;
        }

        uint32_t untexturedColour(uint32_t rgba)
        {
            auto half = [](uint32_t c) { return ((c & 0xFFu) + 1u) >> 1; };
            return (rgba & 0xFF000000u) | half(rgba) | (half(rgba >> 8) << 8) | (half(rgba >> 16) << 16);
        }

        // Two triangles of the quad a (top left), b (top right), c (bottom left), d (bottom right).
        void quad(Pending &p, const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d)
        {
            p.vertices.insert(p.vertices.end(), {a, b, c, b, d, c});
        }

        void queue(Pending &&p)
        {
            if (!p.vertices.empty())
                g_pending.push_back(std::move(p));
        }

        void flush()
        {
            bozo::render::Renderer &renderer = bozo::render::Renderer::instance();
            for (const Pending &p : g_pending)
            {
                if (p.texture)
                    renderer.draw(p.material, kIdentity, p.vertices.data(), static_cast<uint32_t>(p.vertices.size()), p.texture);
                else
                    renderer.draw(p.material, kIdentity, p.vertices.data(), static_cast<uint32_t>(p.vertices.size()));
            }
            g_pending.clear();
        }

        void finish(R5900Context *ctx) { ctx->pc = getRegU32(ctx, 31); }

        // 0x19F940(descr, params): the image at (x, y) around its pivot, scaled to the params' size.
        void sprite(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            const Guest g{rdram};
            const uint32_t descr = getRegU32(ctx, 4), params = getRegU32(ctx, 5);
            finish(ctx);
            const Image im = image(g, descr);
            const int32_t z = g.s32(params + 0x10);
            if (z < 0 || im.width <= 0 || im.height <= 0)
                return;
            TexturePtr tex = texture(g, im);
            if (!tex)
                return;
            const int32_t w16 = g.s32(params + 0x14), h16 = g.s32(params + 0x18);
            const int32_t x0 = g.s32(params + 0x8) + kCentre - 8 - (w16 * g.s32(params + 0x0)) / im.width;
            const int32_t y0 = g.s32(params + 0xC) + kCentre - 4 - (h16 * g.s32(params + 0x4)) / im.height;
            const float xa = x0 / 16.0f, xb = (x0 + w16) / 16.0f, ya = y0 / 16.0f, yb = (y0 + h16) / 16.0f;
            const uint32_t rgba = g.u32(params + 0x30);
            const float fog = fogOf(g, params), zf = static_cast<float>(z);
            Pending p{material(g, params, true, true), {}, tex};
            quad(p, vertex(xa, ya, zf, 0, 0, rgba, fog), vertex(xb, ya, zf, 1, 0, rgba, fog), vertex(xa, yb, zf, 0, 1, rgba, fog),
                 vertex(xb, yb, zf, 1, 1, rgba, fog));
            queue(std::move(p));
        }

        // 0x1A1350(descr, params): the same sprite turned by the angle at +0x1C (65536 a full turn) around its pivot.
        void rotatedSprite(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            const Guest g{rdram};
            const uint32_t descr = getRegU32(ctx, 4), params = getRegU32(ctx, 5);
            finish(ctx);
            const Image im = image(g, descr);
            const int32_t z = g.s32(params + 0x10);
            if (z < 0 || im.width <= 0 || im.height <= 0)
                return;
            TexturePtr tex = texture(g, im);
            if (!tex)
                return;
            const float theta = static_cast<float>(g.u16(params + 0x1C)) * 9.5873799e-05f;
            const float s = std::sin(theta), c = std::cos(theta);
            const float sx = static_cast<float>(g.s32(params + 0x14)) / static_cast<float>(im.width);
            const float sy = static_cast<float>(g.s32(params + 0x18)) / static_cast<float>(im.height);
            const float px = static_cast<float>(g.s32(params + 0x0)), py = static_cast<float>(g.s32(params + 0x4));
            const float bx = static_cast<float>(g.s32(params + 0x8) + kCentre - 8);
            const float by = static_cast<float>(g.s32(params + 0xC) + kCentre - 4);
            const uint32_t rgba = g.u32(params + 0x30);
            const float fog = fogOf(g, params), zf = static_cast<float>(z);
            auto corner = [&](float tx, float ty, float u, float v) {
                const float du = tx - px, dv = ty - py;
                return vertex((bx + c * sx * du + s * sy * dv) / 16.0f, (by - s * sx * du + c * sy * dv) / 16.0f, zf, u, v, rgba, fog);
            };
            const float w = static_cast<float>(im.width), h = static_cast<float>(im.height);
            Pending p{material(g, params, true, true), {}, tex};
            quad(p, corner(0, 0, 0, 0), corner(w, 0, 1, 0), corner(0, h, 0, 1), corner(w, h, 1, 1));
            queue(std::move(p));
        }

        // 0x1A1FF0(params): an untextured rectangle from (x, y) to the absolute corner at +0x14 / +0x18.
        void fill(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            const Guest g{rdram};
            const uint32_t params = getRegU32(ctx, 4);
            finish(ctx);
            const int32_t z = g.s32(params + 0x10);
            if (z < 0)
                return;
            const float x0 = (g.s32(params + 0x8) + kCentre - 8) / 16.0f, y0 = (g.s32(params + 0xC) + kCentre - 4) / 16.0f;
            const float x1 = (g.s32(params + 0x14) + kCentre - 8) / 16.0f, y1 = (g.s32(params + 0x18) + kCentre - 4) / 16.0f;
            const uint32_t rgba = untexturedColour(g.u32(params + 0x30));
            const float fog = fogOf(g, params), zf = static_cast<float>(z);
            Pending p{material(g, params, false, false), {}, nullptr};
            quad(p, vertex(x0, y0, zf, 0, 0, rgba, fog), vertex(x1, y0, zf, 0, 0, rgba, fog), vertex(x0, y1, zf, 0, 0, rgba, fog),
                 vertex(x1, y1, zf, 0, 0, rgba, fog));
            queue(std::move(p));
        }

        struct Batch
        {
            uint32_t descr = 0;
            Pending pending;
            bool open = false;
        };
        Batch g_triangles, g_sprites;

        void closeBatch(Batch &b)
        {
            if (b.open)
                queue(std::move(b.pending));
            b = Batch{};
        }

        // 0x1A2ED8(descr): a batch of textured Gouraud triangles with perspective ST, the image's first tile as texture.
        void triangleBegin(uint8_t *, R5900Context *ctx, Runtime *)
        {
            closeBatch(g_triangles);
            g_triangles.descr = getRegU32(ctx, 4);
            finish(ctx);
        }

        void triangle(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            const Guest g{rdram};
            const uint32_t vertices[3] = {getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6)};
            finish(ctx);
            Batch &b = g_triangles;
            if (!b.descr)
                return;
            const Image im = image(g, b.descr);
            if (!b.open)
            {
                TexturePtr tex = texture(g, im, true);
                if (!tex)
                    return;
                b.pending = Pending{material(g, vertices[0], true, true), {}, tex};
                b.pending.material.useQ = true;
                b.open = true;
            }
            const Texture &tex = *b.pending.texture;
            auto log2up = [](uint32_t n) { uint32_t l = 0; while ((1u << l) < n) ++l; return l; };
            const float su = static_cast<float>(1u << log2up(tex.width)) / static_cast<float>(tex.width);
            const float sv = static_cast<float>(1u << log2up(tex.height)) / static_cast<float>(tex.height);
            for (uint32_t v : vertices)
            {
                const float q = g.f32(v + 0x1C);
                Vertex out = vertex((g.s32(v + 0x8) + kCentre) / 16.0f, (g.s32(v + 0xC) + kCentre) / 16.0f,
                                    static_cast<float>(g.s32(v + 0x10)), q != 0.0f ? g.f32(v + 0x20) / q * su : 0.0f,
                                    q != 0.0f ? g.f32(v + 0x24) / q * sv : 0.0f, g.u32(v + 0x30), fogOf(g, v));
                out.q = q;
                b.pending.vertices.push_back(out);
            }
        }

        void triangleEnd(uint8_t *, R5900Context *ctx, Runtime *)
        {
            closeBatch(g_triangles);
            finish(ctx);
        }

        // 0x1A3CE8(descr) / 0x1A3D20(params) / 0x1A3F80(): untiled sprites of one image.
        void spriteBatchBegin(uint8_t *, R5900Context *ctx, Runtime *)
        {
            closeBatch(g_sprites);
            g_sprites.descr = getRegU32(ctx, 4);
            finish(ctx);
        }

        void spriteBatchDraw(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            const Guest g{rdram};
            const uint32_t params = getRegU32(ctx, 4);
            finish(ctx);
            Batch &b = g_sprites;
            if (!b.descr)
                return;
            const Image im = image(g, b.descr);
            const int32_t z = g.s32(params + 0x10);
            if (z < 0 || im.width <= 0 || im.height <= 0)
                return;
            if (!b.open)
            {
                TexturePtr tex = texture(g, im);
                if (!tex)
                    return;
                b.pending = Pending{material(g, params, true, true), {}, tex};
                b.pending.material.fog = false;
                b.open = true;
            }
            const int32_t w16 = g.s32(params + 0x14), h16 = g.s32(params + 0x18);
            const int32_t x0 = g.s32(params + 0x8) + kCentre - 8 - (w16 * g.s32(params + 0x0)) / im.width;
            const int32_t y0 = g.s32(params + 0xC) + kCentre - 4 - (h16 * g.s32(params + 0x4)) / im.height;
            const float xa = x0 / 16.0f, xb = (x0 + w16) / 16.0f, ya = y0 / 16.0f, yb = (y0 + h16) / 16.0f;
            const uint32_t rgba = g.u32(params + 0x30);
            const float zf = static_cast<float>(z);
            quad(b.pending, vertex(xa, ya, zf, 0, 0, rgba, 1), vertex(xb, ya, zf, 1, 0, rgba, 1), vertex(xa, yb, zf, 0, 1, rgba, 1),
                 vertex(xb, yb, zf, 1, 1, rgba, 1));
        }

        void spriteBatchEnd(uint8_t *, R5900Context *ctx, Runtime *)
        {
            closeBatch(g_sprites);
            finish(ctx);
        }

        // 0x19A510: the 2D drawn since the last splice goes into the frame here, after the 3D before it.
        void splice(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            func_0x19a510(rdram, ctx, runtime);
            closeBatch(g_triangles);
            closeBatch(g_sprites);
            flush();
        }
    }

    void present()
    {
        bozo::render::Renderer &renderer = bozo::render::Renderer::instance();
        if (!g_started)
        {
            g_started = true;
            renderer.setEnabled(true);
            renderer.setPresentsAlone(true);
            renderer.beginFrame(screen());
            return;
        }
        flush();
        renderer.endFrame();
        renderer.beginFrame(screen());
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.registerFunction(kSprite, &sprite);
        runtime.registerFunction(kRotatedSprite, &rotatedSprite);
        runtime.registerFunction(kFill, &fill);
        runtime.registerFunction(kTriangleBegin, &triangleBegin);
        runtime.registerFunction(kTriangle, &triangle);
        runtime.registerFunction(kTriangleEnd, &triangleEnd);
        runtime.registerFunction(kSpriteBatchBegin, &spriteBatchBegin);
        runtime.registerFunction(kSpriteBatchDraw, &spriteBatchDraw);
        runtime.registerFunction(kSpriteBatchEnd, &spriteBatchEnd);
        runtime.registerFunction(kSplice, &splice);
    }
}
