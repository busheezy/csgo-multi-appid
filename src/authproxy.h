#pragma once

namespace authproxy
{
// Unconditional: this is what the plugin is for, and there is nothing to
// configure -- both appids are fixed constants, and the validator validates
// whichever one the server is not pinned to.
void Init();

// The engine creates its Steam game server session at map load, well after
// plugins load, so the hook is installed on the first frame it exists.
void Tick();

void Shutdown();
}
