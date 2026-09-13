// A per-frame callback that survives hibernation.
//
// The obvious place for one is IServerPluginCallbacks::GameFrame, and it is the
// wrong place. SV_Think returns early on a hibernating server:
//
//     if ( sv.IsHibernating() )
//     {
//         // if we're hibernating, just sleep for a while and do not call
//         // server.dll to run a frame
//         NET_SleepUntilMessages( nMilliseconds );
//         return;
//     }
//
// and g_pServerPluginHandler->GameFrame() is further down the same function, so
// it never runs. An empty server hibernates immediately -- before the first
// client connects, and on a fresh server before plugins have even loaded -- so
// a plugin that only ticks on GameFrame does nothing at all on exactly the
// server a client is about to connect to.
//
// serverGameDLL->Think() is the first statement in SV_Frame, ahead of the
// hibernation check and of the !sv.IsActive() early-out, so hooking that gets a
// tick in every state the server can be in. It is a virtual call, so this is
// the same vtable write used elsewhere rather than a detour into engine code.
#pragma once

namespace gametick
{
typedef void *( *CreateInterfaceFn )( const char *pName, int *pReturnCode );
typedef void ( *Callback )();

// Takes the game DLL factory the engine passes to IServerPluginCallbacks::Load.
bool Install( CreateInterfaceFn gameServerFactory, Callback pfnTick );

// False if the hook could not be installed, in which case the caller should
// fall back to ticking from GameFrame and accept the hibernation gap.
bool Installed();

void Remove();
}
