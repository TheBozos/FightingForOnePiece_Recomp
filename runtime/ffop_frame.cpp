#include "ffop_port.h"

#include "ffop_functions.h"

#include "bozo/ee/kernel.h"

#include <cstdint>

namespace ffop::frame
{
    namespace
    {
        constexpr uint32_t kFrameWait = 0x0019E718u;
        constexpr uint32_t kMeterStart = 0x001A8410u;
        constexpr uint32_t kMeterEnd = 0x001A845Cu;

        // bozo:native kMeter
        constexpr uint32_t kMeter[] = {0x001A8410u, 0x001A845Cu};

        void frameWait(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            ffop::draw2d::present();
            bozo::port::frameEnd(runtime, ctx);
            func_0x19e718(rdram, ctx, runtime);
        }
    }

    // The frame's load meter (0x1A83F8 at its start, 0x1A8440 at its end) reads T3, the kernel's H-blank counter.
    bool instruction(R5900Context *ctx, Runtime *runtime, uint32_t pc)
    {
        if (pc != kMeterStart && pc != kMeterEnd)
            return false;
        const uint64_t lines = bozo::port::now(runtime) / bozo::port::kernel(runtime).hblank();
        setRegU64(ctx, pc == kMeterStart ? 2 : 7, lines & 0xFFFFu);
        return true;
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.registerFunction(kFrameWait, &frameWait);
        runtime.syncVCallbackAtVblank();
    }
}
