#include "ffop_port.h"

#include <cstdint>
#include <cstring>

namespace
{
    constexpr uint32_t kFfopEntry = 0x0010001Cu;

    constexpr uint32_t kCdromModeSet = 0x001B5A80u;
    constexpr int32_t kCdromModeGpOffset = -0x1C48;

    void cdromModeSet(uint8_t *rdram, R5900Context *ctx, Runtime *)
    {
        const uint32_t mode = getRegU32(ctx, 4);
        const uint32_t at = getRegU32(ctx, 28) + static_cast<uint32_t>(kCdromModeGpOffset);
        std::memcpy(rdram + (at & 0x01FFFFFCu), &mode, sizeof(mode));
        ctx->pc = getRegU32(ctx, 31);
    }

    void install(bozo::port::Registry &runtime)
    {
        runtime.registerFunction(kCdromModeSet, &cdromModeSet);
        ffop::frame::install(runtime);
        ffop::graphics::install(runtime);
        ffop::movies::install(runtime);
        ffop::draw2d::install(runtime);
        ffop::draw2d::installText(runtime);
        ffop::draw3d::install(runtime);
        ffop::vu0::install(runtime);
    }
}

BOZO_PORT("Fighting for One Piece (Japan)", "SLPS_255.45", kFfopEntry, &install)
