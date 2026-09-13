// A second Steam game server session, in this process, logged on as the other
// appid purely so auth tickets issued for it can be validated.
//
// SteamGameServer_Init infers its appid from steam_appid.txt and owns the one
// session steam_api tracks, but the interface underneath takes the appid
// explicitly (ISteamGameServer::InitGameServer), so a second session can be
// built directly from ISteamClient: its own pipe, its own local user, its own
// game server interface. Its callbacks are pumped with Steam_BGetCallback on
// that pipe alone, which leaves the engine's own dispatch untouched.
#pragma once

#include <cstdint>

namespace validator
{
// Creates the session and starts an anonymous logon. Non-blocking: readiness is
// reported by Ready() once Pump() has seen the logon complete.
bool Start();

// Drains this session's callbacks and retires finished auth sessions. Cheap,
// and meant to be called every frame.
void Pump();

// Asks Steam directly rather than reporting something Pump() latched, because
// on a hibernating server Pump() may not have run since the logon completed.
bool Ready();

// The hook fires for every BeginAuthSession on the patched vtable, and this
// session's own calls go through the same one when it turns out the engine and
// the validator share an adapter class. Used to let those through untouched.
bool IsOwnInterface( const void *pInterface );

// This session's own ISteamGameServer, so the hook can go on without waiting
// for the engine to have one of its own.
void *Interface();

// The engine's ISteamGameServer, once it has one. Null until the engine's own
// Steam session exists, which is well after plugins load.
void *EngineInterface();

// Synchronous, and deliberately so. BeginAuthSession decides InvalidTicket,
// GameMismatch and ExpiredTicket on the spot -- that is ticket parsing and the
// ticket-to-SteamID binding, which is the whole identity question. The
// ValidateAuthTicketResponse_t callback that follows carries ban and licence
// status, not identity, so there is nothing here worth blocking the server
// thread for.
bool Validate( uint64_t steamID, const void *pTicket, int cbTicket );

void Stop();
}
