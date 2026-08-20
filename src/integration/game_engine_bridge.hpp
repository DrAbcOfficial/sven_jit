#pragma once

class asIScriptEngine;

namespace svenjit::integration {

using EngineReadyCallback = void (*)(asIScriptEngine*) noexcept;

const char* ConnectGameEngine(
    void* gameFunctions,
    EngineReadyCallback callback) noexcept;

void DisconnectGameEngine() noexcept;

}
