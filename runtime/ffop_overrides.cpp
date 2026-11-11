#include "bozo/context.h"
#include "bozo/port.h"

#include <cstdint>

namespace
{
    constexpr uint32_t kFfopEntry = 0x0010001Cu;

    void install(bozo::port::Registry &)
    {
    }
}

BOZO_PORT("Fighting for One Piece (Japan)", "SLPS_255.45", kFfopEntry, &install)
