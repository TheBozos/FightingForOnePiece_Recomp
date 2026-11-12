// FFOP's 3D drawn natively, shared by the mesh hooks: each VU1 microprogram's work redone in C++ at the EE call that
// queues the mesh (0x198140 rigid, 0x198B68 skinned), its triangles kept in the game's three draw lists and drawn when
// the game links them into the frame (0x199EE0). Textures come from the .tex files the game places in VRAM (0x1A66D0).
#pragma once

#include "bozo/context.h"
#include "bozo/port.h"
#include "bozo/render/renderer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace ffop::draw3d
{
    using bozo::render::Material;
    using bozo::render::Texture;
    using bozo::render::Vertex;
    using TexturePtr = std::shared_ptr<const Texture>;

    struct Guest
    {
        const uint8_t *ram;
        const uint8_t *spr;
        const uint8_t *at(uint32_t address) const
        {
            return (address >> 28) == 7u ? spr + (address & 0x3FFFu) : ram + (address & 0x01FFFFFFu);
        }
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
        uint64_t u64(uint32_t address) const
        {
            uint64_t v;
            std::memcpy(&v, at(address), 8);
            return v;
        }
        float f32(uint32_t address) const
        {
            float v;
            std::memcpy(&v, at(address), 4);
            return v;
        }
    };

    Guest guest(uint8_t *rdram, Runtime *runtime);

    struct Vec4
    {
        float x = 0, y = 0, z = 0, w = 0;
    };
    // Rows, as the VU programs keep them: v' = v.x * r[0] + v.y * r[1] + v.z * r[2] + v.w * r[3].
    using Mat4 = std::array<Vec4, 4>;

    Vec4 vec4(const Guest &g, uint32_t address);
    Mat4 mat4(const Guest &g, uint32_t address);
    Mat4 mul(const Mat4 &a, const Mat4 &b);
    Vec4 point(const Mat4 &m, float x, float y, float z);

    // The perspective the programs apply (qw18 / qw19 of a rigid draw, qw14 / qw15 of a skinned one) and the guard
    // band past which a triangle is dropped where no clipper runs.
    struct Projection
    {
        float px = 0, py = 0, pz = 0, ox = 0, oy = 0, oz = 0;
        Vec4 guard;
        // View space to GS screen as Renderer::draw takes it: w = -z, positive in front of the camera.
        void matrix(float out[16]) const;
        float screenX(const Vec4 &v) const { return (px * v.x + ox * v.z) / v.z; }
        float screenY(const Vec4 &v) const { return (py * v.y + oy * v.z) / v.z; }
        float screenZ(const Vec4 &v) const { return (pz + oz * v.z) / v.z; }
        bool inside(const Vec4 &v) const;
    };
    Projection projection(const Guest &g, uint32_t scale, uint32_t offset, uint32_t guard);

    // The GS registers a mesh's own A+D blocks set.
    struct GsRegs
    {
        uint64_t tex0 = 0, tex1 = 0, clamp = 0, alpha = 0x44;
        void set(uint32_t reg, uint64_t value);
    };

    enum class List : uint8_t
    {
        Opaque,
        DepthOnly,
        Translucent,
    };

    struct Batch
    {
        Material material;
        TexturePtr texture;
        float matrix[16] = {};
        uint8_t minAlpha = 0x80;
        std::vector<Vertex> vertices;
    };

    struct Draw
    {
        List list = List::Opaque;
        float key = 0;
        int16_t index = 0;
        std::vector<Batch> batches;
    };

    // Triangles into a draw, one batch per run of the same GS state.
    class Builder
    {
    public:
        Builder(Draw &draw, uint32_t fogColour, int32_t bank) : m_draw(draw), m_fogColour(fogColour), m_bank(bank) {}
        void triangle(const GsRegs &gs, uint32_t prim, const float matrix[16], const Vertex &a, const Vertex &b,
                      const Vertex &c);

    private:
        Draw &m_draw;
        uint32_t m_fogColour;
        int32_t m_bank;
        GsRegs m_gs;
        uint32_t m_prim = ~0u;
        const float *m_matrix = nullptr;
        Batch *m_batch = nullptr;
        uint8_t m_textureAlpha = 0x80;
    };

    // The draw-list a mesh goes to, from the list base the game passes.
    List listOf(uint32_t base);
    void queue(Draw &&draw);
    uint32_t fogColour(const Guest &g);
    // The fog block (qw 0x2B3680: -far, fog offset, fog scale, -near) as 0x198038 sets it for an object at depth
    // `key`; `none`: as it stands (a pass without fog).
    Vec4 fog(const Guest &g, float key, bool none);
    int32_t residentBank(const Guest &g, R5900Context *ctx);

    // The texture a TEX0 names in the bank resident at the draw, and its lowest alpha (GS units).
    TexturePtr texture(uint64_t tex0, int32_t bank, uint8_t &minAlpha);

    // A mesh packet (DMA CNT .. RET of VIF codes): each batch the VU program would run (UNPACK V4-32 at TOPS, then
    // MSCNT / MSCAL) handed to `batch` as the qwords of its input buffer.
    using Qword = std::array<uint32_t, 4>;
    bool walkMeshPacket(const Guest &g, uint32_t packet, std::vector<Qword> &buffer, void (*kick)(void *, const std::vector<Qword> &),
                        void *user);
    template <typename F>
    void walkMesh(const Guest &g, uint32_t packet, F &&batch)
    {
        using Fn = std::remove_reference_t<F>;
        std::vector<Qword> buffer(1024);
        walkMeshPacket(g, packet, buffer, [](void *user, const std::vector<Qword> &b) { (*static_cast<Fn *>(user))(b); }, &batch);
    }

    inline float asFloat(uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }

    uint32_t colour(float r, float g, float b, float a);

    void installTextures(bozo::port::Registry &runtime);
    void installRigid(bozo::port::Registry &runtime);
}
