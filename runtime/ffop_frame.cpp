#include "ffop_port.h"

#include "ffop_functions.h"

#include <cstdint>

namespace ffop::frame
{
    namespace
    {
        constexpr uint32_t kFrameWait = 0x0019E718u;

        void frameWait(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            bozo::port::frameEnd(runtime, ctx);
            func_0x19e718(rdram, ctx, runtime);
        }
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.registerFunction(kFrameWait, &frameWait);
        runtime.syncVCallbackAtVblank();
    }
}
