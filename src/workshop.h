// Makes host_workshop_map work on a pinned server.
//
// The game DLL rejects any workshop item whose consumer_appid is not
// engine->GetAppID(). Every CS:GO workshop item is published under 730, so a
// server pinned to 4465480 fails every item with
//
//   UGC file info consumer_appid 730 != engine 4465480
//
// This replaces the "ask the engine for its appid" half of that comparison with
// "accept either of the two appids this build ships under". Everything else
// about the check, including the warning and all the other field validation, is
// left alone.
#pragma once

namespace workshop
{
// No-op unless this is a dedicated server and the check is found exactly once.
void Apply();

void Restore();
}
