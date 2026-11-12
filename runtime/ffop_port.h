#pragma once

#include "bozo/context.h"
#include "bozo/port.h"
#include "bozo/render/renderer.h"

#include <memory>
#include <vector>

namespace ffop::frame
{
    // The load meter's T3 reads answered natively (true when `pc` is one).
    bool instruction(R5900Context *ctx, Runtime *runtime, uint32_t pc);
    void install(bozo::port::Registry &runtime);
}

namespace ffop::vu0
{
    void install(bozo::port::Registry &runtime);
}

namespace ffop::draw2d
{
    // The frame's end: the 2D still pending is drawn, the frame published and the next one begun.
    void present();
    // A batch drawn with the 2D at the next splice, after what is pending.
    void queue(const bozo::render::Material &material, std::vector<bozo::render::Vertex> &&vertices,
               std::shared_ptr<const bozo::render::Texture> texture);
    void install(bozo::port::Registry &runtime);
}

namespace ffop::draw3d
{
    void install(bozo::port::Registry &runtime);
}

namespace ffop::movies
{
    // The player's T0 accesses answered natively (true when `pc` is one).
    bool instruction(R5900Context *ctx, Runtime *runtime, uint32_t pc);
    void install(bozo::port::Registry &runtime);
}

namespace ffop::graphics
{
    struct DispEnv
    {
        uint64_t pmode, smode2, dispfb, display, bgcolor;
    };
    extern DispEnv g_display;

    void install(bozo::port::Registry &runtime);
}
