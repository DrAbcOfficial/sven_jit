#include "jit/jit_service.hpp"

#include <as_jit_x86.h>

namespace svenjit::jit {
namespace {

void* g_jitEngine = nullptr;

}

bool Install(asIScriptEngine* engine) noexcept {
    if (g_jitEngine) {
        return true;
    }

    g_jitEngine = AsJitCreateEngine(engine);
    return g_jitEngine != nullptr;
}

}
