#include "ffop_audio.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace ffop::audio
{
    namespace
    {
        constexpr uint32_t kHeapStart = 0x00034000u;
        constexpr uint32_t kSdrCallbackServer = 0x80000704u;

        std::string moduleName(const std::string &path)
        {
            const size_t slash = path.find_last_of("\\/:");
            std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });
            return name;
        }
    }

    void attach(bozo::libs::Libraries &libraries)
    {
        static std::unique_ptr<bozo::native::irx::CompiledLibraries> host;
        host = std::make_unique<bozo::native::irx::CompiledLibraries>(libraries, kHeapStart);
        std::vector<std::string> names;
        for (auto &image : images())
        {
            names.push_back(image.name);
            host->addImage(std::move(image));
        }
        auto *h = host.get();
        libraries.sif.moduleLoader = [h, names, others = int32_t(0)](const std::string &path) mutable
        {
            const std::string name = moduleName(path);
            if (std::find(names.begin(), names.end(), name) != names.end())
                return h->startImage(name);
            return 0x1000 + ++others;
        };
        libraries.sif.transfer = [h](uint32_t address, const uint8_t *data, uint32_t bytes) { h->writeBuffer(address, data, bytes); };
        libraries.sif.allocateBuffer = [h](uint32_t bytes) { return h->allocateBuffer(bytes); };
        libraries.sif.freeBuffer = [h](uint32_t address) { return h->freeBuffer(address); };
        libraries.sif.resetProvider = [h] { h->reset(); };
        libraries.audioProvider = [h](uint64_t cycle) { return h->advanceTo(cycle); };
        libraries.sif.eeServers[kSdrCallbackServer] = [](uint32_t command, std::span<const uint8_t>, std::span<uint8_t>) {
            throw std::runtime_error(std::format("FFOP: SDRDRV called libsdr's EE callback server (command {}), "
                                                 "which has no native answer yet", command));
        };
    }
}
