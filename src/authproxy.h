#pragma once

namespace authproxy
{
// On by default -- it is what the plugin is for. -nocrossappid turns it off,
// and there is nothing else to configure: both appids are fixed constants.
void Init();

// The engine creates its Steam game server session at map load, well after
// plugins load, so the hook is installed on the first frame it exists.
void Tick();

void Shutdown();
}
