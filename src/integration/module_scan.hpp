#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace svenjit::integration {

constexpr std::uint8_t kWildcard = 0x2A;

struct MemoryRange {
    std::uint8_t* begin;
    std::size_t size;
};

struct ModuleView {
    std::vector<MemoryRange> executableRanges;

    bool ContainsExecutable(const void* address) const noexcept {
        const auto value = reinterpret_cast<std::uintptr_t>(address);
        for (const MemoryRange& range : executableRanges) {
            const auto begin = reinterpret_cast<std::uintptr_t>(range.begin);
            if (value >= begin && value < begin + range.size) {
                return true;
            }
        }
        return false;
    }
};

// Anchor on the loaded GameDLL from a metamod gamedll_funcs_t pointer.
void* FindGameDllAnchor(const void* gameFunctions) noexcept;

// modulePath is Linux-only and used for symbol lookups; it stays nullptr on Windows.
bool BuildModuleView(
    void* anchor,
    ModuleView& module,
    const char** modulePath = nullptr);

bool MatchesPattern(
    const std::uint8_t* address,
    const std::uint8_t* pattern,
    std::size_t patternSize) noexcept;

std::vector<std::uint8_t*> FindPattern(
    const ModuleView& module,
    const std::uint8_t* pattern,
    std::size_t patternSize);

// Writes into executable module memory, restoring the original protection.
bool WriteExecutableMemory(
    void* address,
    const void* bytes,
    std::size_t size) noexcept;

}
