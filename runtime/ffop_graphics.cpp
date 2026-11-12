#include "ffop_port.h"

#include "bozo/context.h"
#include "bozo/port.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace ffop::graphics
{
    DispEnv g_display{};

    namespace
    {
        constexpr uint32_t kSyncVCsrRead = 0x0019E60Cu;
        constexpr uint32_t kPutDispEnv = 0x00197858u;

        constexpr uint32_t kAnswered[] = {
            0x001B5D40u, 0x001B5DA8u,
            0x001B5E50u, 0x001B5F58u, 0x001B6050u,
            0x00197330u,
            0x00193CC0u,
        };

        // bozo:native kHardwareOutput
        constexpr uint32_t kHardwareOutput[] = {0x0019E60Cu, 0x001B7DB0u, 0x001B7DB4u};

        constexpr auto kSorted = [] {
            auto sites = std::to_array(kHardwareOutput);
            std::sort(sites.begin(), sites.end());
            return sites;
        }();

        void putDispEnv(uint8_t *rdram, R5900Context *ctx, Runtime *)
        {
            std::memcpy(&g_display, rdram + (getRegU32(ctx, 4) & 0x01FFFFF0u), sizeof(g_display));
            ctx->pc = getRegU32(ctx, 31);
        }

        void returnZero(uint8_t *, R5900Context *ctx, Runtime *)
        {
            setReturnU32(ctx, 0);
            ctx->pc = getRegU32(ctx, 31);
        }

        bool instruction(uint8_t *, R5900Context *ctx, Runtime *rt, uint32_t pc)
        {
            if (!std::binary_search(kSorted.begin(), kSorted.end(), pc))
                return ffop::movies::instruction(ctx, rt, pc);
            if (pc == kSyncVCsrRead)
                setRegU64(ctx, 3, (bozo::port::vblankCount(rt) & 1u) << 13);
            return true;
        }
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.nativeGraphics(&instruction);
        for (uint32_t address : kAnswered)
            runtime.registerFunction(address, &returnZero);
        runtime.registerFunction(kPutDispEnv, &putDispEnv);
    }
}
