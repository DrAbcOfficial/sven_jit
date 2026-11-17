#pragma once

namespace svenjit::integration {

// Locates the GameDLL's AngelScript context callback registration and redirects
// the request-context callback through a hook that honors the `as_line_callback`
// cvar. Returns an error string, or nullptr on success; a failure never affects
// the JIT itself and should only be reported.
const char* ConnectLineCallbackPatch(const void* gameFunctions) noexcept;

void DisconnectLineCallbackPatch() noexcept;

}
