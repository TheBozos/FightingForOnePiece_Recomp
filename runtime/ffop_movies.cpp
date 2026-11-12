#include "ffop_port.h"

#include "ffop_functions.h"

#include "bozo/render/movie.h"
#include "bozo/render/renderer.h"

#include <cstdint>
#include <cstring>
#include <memory>

namespace ffop::movies
{
    namespace
    {
        constexpr uint32_t kPlayMovie = 0x001B6528u;
        constexpr uint64_t kBackdropKey = 0x464F50'4D4F5649ull;

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

        void playMovie(uint8_t *rdram, R5900Context *ctx, Runtime *runtime)
        {
            func_0x1b6528(rdram, ctx, runtime);
            bozo::render::Renderer::instance().clearBackdrop();
        }
    }

    void install(bozo::port::Registry &runtime)
    {
        bozo::render::setMoviePictureHook(&picture);
        runtime.registerFunction(kPlayMovie, &playMovie);
    }
}
