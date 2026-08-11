#pragma once

class asIScriptEngine;

namespace svenjit::integration {

using EngineReadyCallback = void (*)(asIScriptEngine*) noexcept;

const char* ConnectAsext(
    void* utilities,
    void* plugin,
    EngineReadyCallback callback) noexcept;

}
