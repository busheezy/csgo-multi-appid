// Cross-appid client validation.
//
// A game server can only validate auth tickets for the app it logged on as, so
// a client running the same build under the other appid fails with
// k_EBeginAuthSessionResultGameMismatch. The tempting "fix" is to treat that
// result as success, but the SteamID the engine uses comes from the client's
// own connect packet and is only trustworthy once Steam has validated the
// ticket against it -- so accepting blindly lets anyone claim any SteamID.
//
// Instead the ticket goes to a second Steam session in this process, logged on
// as the other appid (see validator.h). BeginAuthSession there binds ticket to
// SteamID exactly as it would for a native client, so a forged ID is rejected
// and the engine's own rejection stands.
//
// Only GameMismatch is diverted. Invalid, expired, duplicate and version
// mismatch tickets keep the engine's own answer.

#include "authproxy.h"
#include "appid.h"
#include "platform.h"
#include "steam_min.h"
#include "validator.h"

#include <cstring>

namespace authproxy
{
namespace
{

// ISteamGameServer slot 29; see steam_min.h for the full layout.
const int kBeginAuthSessionSlot = 29;
const int kMaxTicketBytes = 1400;

#if defined( _WIN32 )
// Virtuals are __thiscall; __fastcall with an unused edx slot gives a free
// function the same register layout.
#define HOOK_CALL __fastcall
#define HOOK_THIS steam::ISteamGameServer *pThis, void * /*edx*/
#define HOOK_FORWARD pThis, nullptr
typedef steam::EBeginAuthSessionResult( HOOK_CALL *BeginAuthSessionFn )(
	steam::ISteamGameServer *, void *, const void *, int, uint64_t );
#else
// Itanium ABI: this is simply the first argument.
#define HOOK_CALL
#define HOOK_THIS steam::ISteamGameServer *pThis
#define HOOK_FORWARD pThis
typedef steam::EBeginAuthSessionResult( *BeginAuthSessionFn )(
	steam::ISteamGameServer *, const void *, int, uint64_t );
#endif

bool				s_bEnabled = true;
bool				s_bHooked;
bool				s_bValidatorStarted;
BeginAuthSessionFn	s_pfnOriginal;
void				**s_pVTableSlot;
steam::Api			s_Steam;

steam::EBeginAuthSessionResult HOOK_CALL Hook_BeginAuthSession( HOOK_THIS, const void *pTicket, int cbTicket, uint64_t steamID )
{
	const steam::EBeginAuthSessionResult result = s_pfnOriginal( HOOK_FORWARD, pTicket, cbTicket, steamID );

	// Every other verdict, including every other kind of failure, is the
	// engine's to act on unchanged.
	if ( result != steam::k_EBeginAuthSessionResultGameMismatch )
		return result;

	if ( cbTicket <= 0 || cbTicket > kMaxTicketBytes )
		return result;

	if ( !validator::Validate( steamID, pTicket, cbTicket ) )
		return result;

	plat::Log( "csgo-multi-appid: %llu validated for appid %u\n",
			   (unsigned long long)steamID, appid::Other() );
	return steam::k_EBeginAuthSessionResultOK;
}

} // namespace

void Init()
{
	char buf[ 8 ];
	if ( plat::CommandLineValue( "-nocrossappid", buf, sizeof( buf ) ) )
	{
		s_bEnabled = false;
		plat::Log( "csgo-multi-appid: cross-appid validation disabled\n" );
		return;
	}

	plat::Log( "csgo-multi-appid: clients whose tickets are for appid %u will be validated separately\n",
			   appid::Other() );
}

void Tick()
{
	if ( !s_bEnabled )
		return;

	// The engine creates its own Steam session at map load, long after plugins
	// load, so everything here waits for that before doing anything.
	if ( !s_bHooked )
	{
		if ( !s_Steam.GetHSteamUser && !s_Steam.Load() )
			return;

		steam::ISteamGameServer *pGameServer = s_Steam.EngineGameServer();
		if ( !pGameServer )
			return; // no session yet, try again next frame

		void **pVTable = *(void ***)pGameServer;
		s_pVTableSlot = &pVTable[ kBeginAuthSessionSlot ];
		s_pfnOriginal = (BeginAuthSessionFn)*s_pVTableSlot;

		void *pHook = (void *)&Hook_BeginAuthSession;
		if ( !plat::WriteMemory( s_pVTableSlot, &pHook, sizeof( pHook ) ) )
		{
			plat::Warn( "csgo-multi-appid: could not install the BeginAuthSession hook\n" );
			s_bEnabled = false;
			return;
		}

		s_bHooked = true;
	}

	if ( !s_bValidatorStarted )
	{
		s_bValidatorStarted = true;
		if ( !validator::Start() )
			plat::Warn( "csgo-multi-appid: validator session could not start;"
						" clients from appid %u will keep being rejected\n", appid::Other() );
		return;
	}

	validator::Pump();
}

void Shutdown()
{
	if ( s_bValidatorStarted )
	{
		validator::Stop();
		s_bValidatorStarted = false;
	}

	if ( !s_bHooked )
		return;

	void *pOriginal = (void *)s_pfnOriginal;
	plat::WriteMemory( s_pVTableSlot, &pOriginal, sizeof( pOriginal ) );
	s_bHooked = false;
}

} // namespace authproxy
