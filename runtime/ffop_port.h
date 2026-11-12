#pragma once

#include "bozo/context.h"
#include "bozo/port.h"

namespace ffop::frame
{
    void install(bozo::port::Registry &runtime);
}

namespace ffop::movies
{
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
