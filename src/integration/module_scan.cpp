#include "integration/module_scan.hpp"

#include "metamod/hlsdk.hpp"

#include <array>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace svenjit::integration {

void* FindGameDllAnchor(const void* gameFunctions) noexcept {
    if (!gameFunctions) {
        return nullptr;
    }

    auto* functions = static_cast<const gamedll_funcs_t*>(gameFunctions);
    if (!functions->dllapi_table) {
        return nullptr;
    }

    const DLL_FUNCTIONS* table = functions->dllapi_table;
    const std::array<void*, 4> candidates = {
        reinterpret_cast<void*>(table->pfnGameInit),
        reinterpret_cast<void*>(table->pfnSpawn),
        reinterpret_cast<void*>(table->pfnServerActivate),
        reinterpret_cast<void*>(table->pfnStartFrame),
    };
    for (void* candidate : candidates) {
        if (candidate) {
            return candidate;
        }
    }
    return nullptr;
}

#ifdef _WIN32

bool BuildModuleView(
    void* anchor,
    ModuleView& module,
    const char**) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(anchor, &memory, sizeof(memory)) || !memory.AllocationBase) {
        return false;
    }

    auto* base = static_cast<std::uint8_t*>(memory.AllocationBase);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        if ((section[index].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }
        const std::size_t size = section[index].Misc.VirtualSize;
        if (size != 0) {
            module.executableRanges.push_back({base + section[index].VirtualAddress, size});
        }
    }
    return !module.executableRanges.empty() && module.ContainsExecutable(anchor);
}

#else

struct ModuleSearchContext {
    std::uintptr_t anchor;
    ModuleView* module;
    const char* path;
};

int FindContainingModule(dl_phdr_info* info, std::size_t, void* data) {
    auto* context = static_cast<ModuleSearchContext*>(data);
    bool containsAnchor = false;
    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type != PT_LOAD) {
            continue;
        }
        const auto begin = static_cast<std::uintptr_t>(info->dlpi_addr + header.p_vaddr);
        if (context->anchor >= begin && context->anchor < begin + header.p_memsz) {
            containsAnchor = true;
            break;
        }
    }
    if (!containsAnchor) {
        return 0;
    }

    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0 && header.p_memsz != 0) {
            context->module->executableRanges.push_back({
                reinterpret_cast<std::uint8_t*>(info->dlpi_addr + header.p_vaddr),
                static_cast<std::size_t>(header.p_memsz),
            });
        }
    }
    context->path = info->dlpi_name;
    return 1;
}

bool BuildModuleView(
    void* anchor,
    ModuleView& module,
    const char** modulePath) {
    ModuleSearchContext context{
        reinterpret_cast<std::uintptr_t>(anchor),
        &module,
        nullptr,
    };
    dl_iterate_phdr(&FindContainingModule, &context);
    if (modulePath) {
        *modulePath = context.path;
    }
    return !module.executableRanges.empty() && module.ContainsExecutable(anchor);
}

#endif

bool MatchesPattern(
    const std::uint8_t* address,
    const std::uint8_t* pattern,
    std::size_t patternSize) noexcept {
    for (std::size_t index = 0; index < patternSize; ++index) {
        if (pattern[index] != kWildcard && address[index] != pattern[index]) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t*> FindPattern(
    const ModuleView& module,
    const std::uint8_t* pattern,
    std::size_t patternSize) {
    std::vector<std::uint8_t*> matches;
    for (const MemoryRange& range : module.executableRanges) {
        if (range.size < patternSize) {
            continue;
        }
        const std::size_t last = range.size - patternSize;
        for (std::size_t offset = 0; offset <= last; ++offset) {
            std::uint8_t* candidate = range.begin + offset;
            if (MatchesPattern(candidate, pattern, patternSize)) {
                matches.push_back(candidate);
            }
        }
    }
    return matches;
}

bool WriteExecutableMemory(
    void* address,
    const void* bytes,
    std::size_t size) noexcept {
    if (!address || !bytes || size == 0) {
        return false;
    }

#ifdef _WIN32
    DWORD oldProtection = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }

    std::memcpy(address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, oldProtection, &ignored);
    return true;
#else
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        return false;
    }
    const auto pageMask = static_cast<std::uintptr_t>(pageSize - 1);
    const auto begin = reinterpret_cast<std::uintptr_t>(address) & ~pageMask;
    const auto end = (reinterpret_cast<std::uintptr_t>(address) + size + pageMask) & ~pageMask;
    if (mprotect(
            reinterpret_cast<void*>(begin),
            end - begin,
            PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    std::memcpy(address, bytes, size);
    __builtin___clear_cache(
        reinterpret_cast<char*>(address),
        reinterpret_cast<char*>(address) + size);
    mprotect(
        reinterpret_cast<void*>(begin),
        end - begin,
        PROT_READ | PROT_EXEC);
    return true;
#endif
}

}
