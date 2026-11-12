// FFOP's 3D textures natively: the texels and CLUTs the bank loader's 0x1A66D0 uploads through 0x197050(packet, DBP,
// DBW, PSM, x, y, width, height, data) kept per bank by their DBP as they are sent (the game frees the file after), and
// a mesh's TEX0 (patched in at the model's load from the record 0x1A66D0 fills) decoded from the uploads at its TBP and
// CBP in the bank resident at the draw.
#include "ffop_port.h"

#include "ffop_3d.h"
#include "ffop_functions.h"

#include "bozo/hash.h"
#include "bozo/render/texture_pack.h"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace ffop::draw3d
{
    namespace
    {
        namespace packs = bozo::render::packs;

        constexpr uint32_t kLoadImage = 0x00197050u;
        constexpr uint32_t kFromPlacement[] = {0x001A67ECu, 0x001A6920u, 0x001A6B50u};
        constexpr int32_t kLoadingBankGp = -0x263C;
        constexpr uint64_t kTexa = 0x80u;
        constexpr int32_t kBanks = 8;

        struct Upload
        {
            uint32_t psm = 0, width = 0, height = 0;
            std::vector<uint8_t> bytes;
        };

        struct Decoded
        {
            uint64_t generation = 0;
            TexturePtr texture;
            uint8_t minAlpha = 0x80;
        };

        struct Bank
        {
            std::unordered_map<uint32_t, Upload> uploads;
            std::unordered_map<uint64_t, Decoded> decoded;
            uint64_t generation = 1;
        };
        Bank g_banks[kBanks];

        uint32_t bitsOf(uint32_t psm)
        {
            switch (psm)
            {
            case packs::PSMCT32: return 32;
            case packs::PSMCT24: return 24;
            case packs::PSMCT16: case packs::PSMCT16S: return 16;
            case packs::PSMT8: return 8;
            case packs::PSMT4: return 4;
            default: return 0;
            }
        }

        // 0x197050 from 0x1A66D0: one CLUT, mip level or band of rows of a level, in the loading bank.
        void loadImage(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const uint32_t ra = getRegU32(ctx, 31);
            if (std::find(std::begin(kFromPlacement), std::end(kFromPlacement), ra) != std::end(kFromPlacement))
            {
                const Guest g = guest(rdram, runtime);
                const int32_t bank = g.s32(getRegU32(ctx, 28) + static_cast<uint32_t>(kLoadingBankGp));
                const uint32_t dbp = getRegU32(ctx, 5) & 0x3FFFu, psm = getRegU32(ctx, 7) & 0x3Fu;
                const uint32_t y = getRegU32(ctx, 9) & 0xFFFFu, width = getRegU32(ctx, 10) & 0xFFFFu, height = getRegU32(ctx, 11) & 0xFFFFu;
                const uint32_t data = g.u32(getRegU32(ctx, 29));
                const uint32_t bits = bitsOf(psm);
                if (bank >= 0 && bank < kBanks && bits && width && height && width <= 2048u && y + height <= 2048u)
                {
                    Bank &b = g_banks[bank];
                    const size_t row = size_t(width) * bits / 8u;
                    Upload &up = b.uploads[dbp];
                    if (y == 0u || up.psm != psm || up.width != width)
                        up = Upload{psm, width, 0, {}};
                    up.height = std::max(up.height, y + height);
                    up.bytes.resize(row * up.height);
                    std::memcpy(&up.bytes[row * y], g.at(data), row * height);
                    ++b.generation;
                }
            }
            func_0x197050(rdram, ctx, runtime);
        }

        bool decode(const Bank &bank, uint64_t tex0, Decoded &out)
        {
            const uint32_t tbp = static_cast<uint32_t>(tex0 & 0x3FFFu), psm = static_cast<uint32_t>((tex0 >> 20) & 0x3Fu);
            const uint32_t tw = 1u << ((tex0 >> 26) & 0xFu), th = 1u << ((tex0 >> 30) & 0xFu);
            const auto texels = bank.uploads.find(tbp);
            if (texels == bank.uploads.end() || texels->second.psm != psm || tw > 1024u || th > 1024u)
                return false;
            const Upload &up = texels->second;
            const bool indexed = packs::psmIndexed(psm);
            uint32_t palette[256] = {};
            uint64_t hash = bozo::hash::xxh64(up.bytes.data(), up.bytes.size(), tex0);
            if (indexed)
            {
                const auto clut = bank.uploads.find(static_cast<uint32_t>((tex0 >> 37) & 0x3FFFu));
                if (clut == bank.uploads.end())
                    return false;
                const Upload &c = clut->second;
                const bool eight = psm == packs::PSMT8;
                const uint32_t size = c.psm == packs::PSMCT32 ? 4u : 2u, csa = static_cast<uint32_t>((tex0 >> 56) & 0x1Fu);
                for (uint32_t i = 0; i < (eight ? 256u : 16u); ++i)
                {
                    uint32_t at = csa * 16u + i;
                    if (eight)
                    {
                        uint32_t x = 0, y = 0;
                        bozo::render::csm1Position(i, x, y);
                        at = y * c.width + x;
                    }
                    if ((at + 1u) * size > c.bytes.size())
                        continue;
                    uint32_t v = 0;
                    std::memcpy(&v, &c.bytes[at * size], size);
                    palette[i] = size == 4u ? v : packs::ct16ToRgba(v, kTexa);
                }
                hash = bozo::hash::xxh64(c.bytes.data(), c.bytes.size(), hash);
            }
            packs::TextureRecord record;
            record.psm = psm == packs::PSMCT24 ? packs::PSMCT32 : psm;
            record.width = up.width;
            record.height = up.height;
            const size_t count = size_t(up.width) * up.height;
            if (psm == packs::PSMT4)
            {
                record.texels.resize(count);
                for (size_t i = 0; i < count; ++i)
                    record.texels[i] = (up.bytes[i >> 1] >> ((i & 1u) * 4u)) & 0xFu;
            }
            else if (psm == packs::PSMCT24)
            {
                record.texels.resize(count * 4u);
                for (size_t i = 0; i < count; ++i)
                {
                    std::memcpy(&record.texels[i * 4u], &up.bytes[i * 3u], 3);
                    record.texels[i * 4u + 3u] = static_cast<uint8_t>(kTexa);
                }
            }
            else
                record.texels = up.bytes;
            auto texture = std::make_shared<Texture>();
            packs::decodeRecord(*texture, tw, th, record.psm, record, palette, kTexa);
            texture->key = hash | 1u;
            texture->version = hash;
            texture->asset = true;
            uint32_t lowest = 0xFFu;
            for (uint32_t y = 0; y < std::min(up.height, th); ++y)
                for (uint32_t x = 0; x < std::min(up.width, tw); ++x)
                    lowest = std::min(lowest, texture->rgba[size_t(y) * tw + x] >> 24);
            out.texture = std::move(texture);
            out.minAlpha = static_cast<uint8_t>(lowest);
            return true;
        }

        const Decoded *find(int32_t index, uint64_t tex0)
        {
            Bank &bank = g_banks[index];
            const uint64_t key = tex0 & 0x1FFFFFFFFFFFFFFFull;
            auto it = bank.decoded.find(key);
            if (it != bank.decoded.end() && it->second.generation == bank.generation)
                return it->second.texture ? &it->second : nullptr;
            Decoded d;
            d.generation = bank.generation;
            decode(bank, tex0, d);
            if (bank.decoded.size() > 4096u)
                bank.decoded.clear();
            const Decoded &stored = bank.decoded[key] = std::move(d);
            return stored.texture ? &stored : nullptr;
        }
    }

    TexturePtr texture(uint64_t tex0, int32_t bank, uint8_t &minAlpha)
    {
        const Decoded *found = nullptr;
        if (bank > 0 && bank < kBanks)
            found = find(bank, tex0);
        for (int32_t i = 0; !found && i < kBanks; ++i)
            if (i != bank)
                found = find(i, tex0);
        if (!found)
        {
            return nullptr;
        }
        minAlpha = found->minAlpha;
        return found->texture;
    }

    void installTextures(bozo::port::Registry &runtime) { runtime.registerFunction(kLoadImage, &loadImage); }
}
