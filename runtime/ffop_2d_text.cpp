// FFOP's text library (0x1AC000-0x1AD900: the memory card messages) drawn natively. The game's own call still runs and
// moves the pen; its glyphs, 24x24 4-bit images of the font at 0x394ED0 with the CLUT at 0x394D50, are drawn as
// the sprites its packet holds.
#include "ffop_port.h"

#include "ffop_functions.h"

#include "bozo/context.h"
#include "bozo/render/renderer.h"

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

        constexpr uint32_t kDrawString = 0x001ACC90u;

        constexpr uint32_t kState = 0x00BC6480u;
        constexpr uint32_t kFont = 0x00394ED0u;
        constexpr uint32_t kClut = 0x00394D50u;
        constexpr uint32_t kGlyphBytes = 0x120u;
        constexpr uint32_t kGlyphSize = 24u;
        constexpr uint16_t kEnd = 0xFFFFu;
        constexpr uint16_t kHalf = 0x8000u;
        constexpr uint16_t kHalfImages = 0xEB7u;
        constexpr uint64_t kTextureKey = 0x464F50'54455854ull;

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
        };

        // The pen and the draw state at kState, as the string starts.
        struct Pen
        {
            int32_t x, y, advance, width, height;
            uint32_t colour, outline;
        };

        Pen pen(const Guest &g)
        {
            return {g.s32(kState), g.s32(kState + 4), g.s32(kState + 0x20), g.s32(kState + 0x28), g.s32(kState + 0x30),
                    g.u32(kState + 0x10), g.u32(kState + 0x14)};
        }

        std::unordered_map<uint32_t, TexturePtr> g_glyphs;

        TexturePtr glyph(const Guest &g, uint32_t index)
        {
            const uint32_t source = kFont + index * kGlyphBytes;
            auto texture = std::make_shared<Texture>();
            texture->width = kGlyphSize;
            texture->height = kGlyphSize;
            texture->rgba.resize(kGlyphSize * kGlyphSize);
            uint32_t clut[16];
            for (uint32_t i = 0; i < 16; ++i)
                clut[i] = g.u32(kClut + i * 4u);
            uint64_t hash = 0xCBF29CE484222325ull;
            for (uint32_t i = 0; i < kGlyphSize * kGlyphSize; ++i)
            {
                const uint8_t byte = g.at(source + i / 2u)[0];
                const uint8_t texel = (i & 1u) ? byte >> 4 : byte & 0xFu;
                texture->rgba[i] = clut[texel];
                hash = (hash ^ texel) * 0x100000001B3ull;
            }
            if (auto it = g_glyphs.find(index); it != g_glyphs.end() && it->second->version == hash)
                return it->second;
            texture->key = kTextureKey ^ (uint64_t(index) << 20);
            texture->version = hash;
            if (g_glyphs.size() > 4096)
                g_glyphs.clear();
            g_glyphs[index] = texture;
            return texture;
        }

        Material material()
        {
            Material m;
            m.textured = true;
            m.textureFunction = Material::TextureFunction::Modulate;
            m.textureAlpha = true;
            m.alphaBlend = true;
            m.blend = Material::Blend::Alpha;
            m.gsBlend = true;
            m.blendA = 0;
            m.blendB = 1;
            m.blendC = 0;
            m.blendD = 1;
            m.depthTest = false;
            m.depthWrite = false;
            m.clampU = m.clampV = true;
            return m;
        }

        void sprite(std::vector<Vertex> &out, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t rgba)
        {
            const float x0 = x / 16.0f, y0 = y / 16.0f, x1 = (x + width) / 16.0f, y1 = (y + height) / 16.0f;
            auto v = [&](float px, float py, float u, float t) {
                Vertex r;
                r.x = px;
                r.y = py;
                r.u = u;
                r.v = t;
                r.rgba = rgba;
                return r;
            };
            out.insert(out.end(), {v(x0, y0, 0, 0), v(x1, y0, 1, 0), v(x0, y1, 0, 1), v(x1, y0, 1, 0), v(x1, y1, 1, 1), v(x0, y1, 0, 1)});
        }

        // 0x1AD7A8: one glyph at the pen, first in the outline colour at the pen and at each offset of the list (pairs
        // to (0, 0)), then in its own colour; without a list once in its own colour.
        void drawGlyph(const Guest &g, const Pen &p, int32_t x, uint32_t index, int32_t width, uint32_t offsets)
        {
            std::vector<Vertex> vertices;
            if (offsets)
            {
                sprite(vertices, x, p.y, width, p.height, p.outline);
                for (uint32_t at = offsets; g.s32(at) != 0 || g.s32(at + 4) != 0; at += 8)
                    sprite(vertices, x + g.s32(at), p.y + g.s32(at + 4), width, p.height, p.outline);
            }
            sprite(vertices, x, p.y, width, p.height, p.colour);
            queue(material(), std::move(vertices), glyph(g, index));
        }

        // 0x1ACC90(packet, glyphs, offsets, halves): the glyph list to its 0xFFFF; with `halves`, flag 0x8000 marks a
        // half-width glyph, else it is part of the index.
        void drawString(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const Guest g{rdram};
            const uint32_t glyphs = getRegU32(ctx, 5), offsets = getRegU32(ctx, 6);
            const bool whole = getRegU32(ctx, 7) == 0;
            const Pen p = pen(g);
            func_0x1acc90(rdram, ctx, runtime);
            int32_t x = p.x;
            for (uint32_t at = glyphs, n = 0; n < 0x100u; at += 2u, ++n)
            {
                const uint16_t code = g.u16(at);
                if (code == kEnd)
                    break;
                if (whole)
                {
                    drawGlyph(g, p, x, code, p.width, offsets);
                    x += p.advance;
                    continue;
                }
                const uint16_t index = code & ~kHalf;
                const bool half = (code & kHalf) != 0;
                drawGlyph(g, p, x, index, half && index < kHalfImages ? p.width / 2 : p.width, offsets);
                x += half ? p.advance / 2 : p.advance;
            }
        }
    }

    void installText(bozo::port::Registry &runtime) { runtime.registerFunction(kDrawString, &drawString); }
}
