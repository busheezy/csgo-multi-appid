// Hands a verdict from our own Steam session to the engine's auth-response
// handler, so a cross-appid client is dealt with by exactly the code that deals
// with a native one: the ban check, the duplicate-SteamID check, the
// "STEAM USERID validated" log line, and the NetworkIDValidated notifications
// to both the plugin chain and the game DLL.
//
// The alternative would be hand-rolling a kick, which would neither tell the
// game DLL anything nor produce the right reject message.
#pragma once

#include <cstdint>

namespace enginefwd
{
// Locates CSteam3Server::OnValidateAuthTicketResponse and the CSteam3Server
// singleton, and refuses unless the candidate really looks like one (its
// m_eServerMode has to be a valid EServerMode). Safe to call repeatedly.
bool Init();

bool Available();

// eAuthSessionResponse is an EAuthSessionResponse; ownerSteamID may be 0.
bool Forward( uint64_t steamID, int eAuthSessionResponse, uint64_t ownerSteamID );
}
