// FFOP's sound modules from the disc (SDRDRV and its LIBSD, the SK sound kit, MODHSYN, MODMSIN, STRADPCM), compiled to
// native code and run by BozoEngine's compiled-library host over native libsd: the game's libsdr and sound RPCs reach
// them.
#pragma once

#include "bozo/libs/libraries.h"
#include "bozo/native/compiled_libraries.h"

#include <vector>

namespace ffop::audio
{
    std::vector<bozo::native::irx::Image> images();
    void attach(bozo::libs::Libraries &libraries);
}
