#pragma once

#include <cstdint>

namespace appid
{
// The two appids the same build ships under. The legacy release is 4465480; the
// shipped csgo/steam.inf still says 730 because the depot was re-published
// unchanged. There is no third possibility, so neither value is configurable.
const uint32_t kAppIdLegacy = 4465480;
const uint32_t kAppIdRetail = 730;

// What the server is pinned to, and the one its clients' tickets may instead
// have been issued for.
inline uint32_t Pinned() { return kAppIdLegacy; }
inline uint32_t Other() { return kAppIdRetail; }

// Pins the effective appid: writes steam_appid.txt, exports SteamAppId, and
// overwrites the engine's parsed steam.inf value so the advertised appid
// matches the one the server logs on with.
void Apply();

// Puts the engine global back. The file and environment variable are left
// alone -- they only matter at the next logon.
void Restore();
}
