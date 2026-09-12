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

bool Ready();

// Synchronous, and deliberately so. BeginAuthSession decides InvalidTicket,
// GameMismatch and ExpiredTicket on the spot -- that is ticket parsing and the
// ticket-to-SteamID binding, which is the whole identity question. The
// ValidateAuthTicketResponse_t callback that follows carries ban and licence
// status, not identity, so there is nothing here worth blocking the server
// thread for.
bool Validate( uint64_t steamID, const void *pTicket, int cbTicket );

void Stop();
}
