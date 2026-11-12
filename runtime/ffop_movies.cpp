#include "ffop_port.h"

#include "ffop_functions.h"

#include "bozo/ee/kernel.h"
#include "bozo/render/movie.h"
#include "bozo/render/renderer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>

namespace ffop::movies
{
    namespace
    {
        constexpr uint32_t kPlayMovie = 0x001B6528u;
        constexpr uint64_t kBackdropKey = 0x464F50'4D4F5649ull;

        constexpr uint32_t kTimerStart = 0x001B81B0u;
        constexpr uint32_t kTimerRestart = 0x001B84E8u;
        constexpr uint32_t kFrameCheck = 0x001B8484u;
        constexpr uint32_t kFrameWait = 0x001B84A0u;
        constexpr uint32_t kFrameLines = 500;
        constexpr uint32_t kFeed = 0x001B89C0u;

        // bozo:native kTimer
        constexpr uint32_t kTimer[] = {0x001B81A4u, 0x001B81B0u, 0x001B8484u, 0x001B84A0u, 0x001B84E4u, 0x001B84E8u};

        uint64_t g_timerStart = 0;
        uint64_t g_version = 0;

        void picture(uint32_t, uint32_t width, uint32_t height, const uint8_t *rgba)
        {
            auto texture = std::make_shared<bozo::render::Texture>();
            texture->key = kBackdropKey;
            texture->version = ++g_version;
            texture->width = width;
            texture->height = height;
            texture->rgba.resize(static_cast<size_t>(width) * height);
            std::memcpy(texture->rgba.data(), rgba, texture->rgba.size() * 4u);
            for (uint32_t &c : texture->rgba)
                c |= 0xFF000000u;
            bozo::render::Screen screen;
            screen.originX = 0.0f;
            screen.originY = 0.0f;
            screen.width = static_cast<float>(width);
            screen.height = static_cast<float>(height);
            bozo::render::Renderer::instance().setBackdrop(std::move(texture), screen, 0.0f, 0.0f, screen.width,
                                                           screen.height, true);
        }

        uint32_t lines(Runtime *runtime)
        {
            return static_cast<uint32_t>((bozo::port::now(runtime) - g_timerStart) / bozo::port::kernel(runtime).hblank());
        }

        void feed(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            const bool waiting = getRegU32(ctx, 31) == kFrameWait;
            func_0x1b89c0(rdram, ctx, runtime);
            if (waiting && lines(runtime) < kFrameLines)
                bozo::port::accountCycles(runtime, ctx, bozo::port::kernel(runtime).hblank());
        }

        void playMovie(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            func_0x1b6528(rdram, ctx, runtime);
            bozo::render::Renderer::instance().clearBackdrop();
        }
    }

    // T0 paces the player at 0x1B8130: the next picture goes once 500 H-blanks have passed since its restart.
    bool instruction(R5900Context *ctx, Runtime *runtime, uint32_t pc)
    {
        switch (pc)
        {
        case kTimerStart:
        case kTimerRestart:
            g_timerStart = bozo::port::now(runtime);
            return true;
        case kFrameCheck:
            setRegU64(ctx, 2, std::min(lines(runtime), kFrameLines));
            return true;
        case kFrameWait:
            setRegU64(ctx, 3, std::min(lines(runtime), kFrameLines));
            return true;
        default:
            return std::find(std::begin(kTimer), std::end(kTimer), pc) != std::end(kTimer);
        }
    }

    void install(bozo::port::Registry &runtime)
    {
        bozo::render::setMoviePictureHook(&picture);
        runtime.registerFunction(kPlayMovie, &playMovie);
        runtime.registerFunction(kFeed, &feed);
    }
}
