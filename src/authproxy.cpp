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

const int kMaxTicketBytes = 1400;

#if defined( _WIN32 )
// Virtuals are __thiscall; __fastcall with an unused edx slot gives a free
// function the same register layout.
#define HOOK_CALL __fastcall
#define HOOK_THIS steam::ISteamGameServer *pThis, void * /*edx*/
#define HOOK_FORWARD pThis, nullptr
typedef steam::EBeginAuthSessionResult( HOOK_CALL *BeginAuthSessionFn )(
	steam::ISteamGameServer *, void *, const void *, int, uint64_t );
typedef void( HOOK_CALL *LogOffFn )( steam::ISteamGameServer *, void * );
#else
// Itanium ABI: this is simply the first argument.
#define HOOK_CALL
#define HOOK_THIS steam::ISteamGameServer *pThis
#define HOOK_FORWARD pThis
typedef steam::EBeginAuthSessionResult( *BeginAuthSessionFn )(
	steam::ISteamGameServer *, const void *, int, uint64_t );
typedef void( *LogOffFn )( steam::ISteamGameServer * );
#endif

// Set only if the hook cannot be installed; there is no way to turn the feature
// off, because a server without it is a server that rejects half its players.
// The engine's ISteamGameServer and the validator's may be instances of one
// adapter class sharing a vtable, or not, and which it is decides whether one
// patch covers both. Rather than depend on the answer, each distinct vtable
// seen gets patched once and restored on unload.
struct Patch_t
{
	void	**ppSlot;
	void	*pOriginal;
};

const int			kMaxPatches = 4;

steam::EBeginAuthSessionResult HOOK_CALL Hook_BeginAuthSession(
	HOOK_THIS, const void *pTicket, int cbTicket, uint64_t steamID );
void HOOK_CALL Hook_LogOff( HOOK_THIS );

bool				s_bBroken;
bool				s_bValidatorStarted;
BeginAuthSessionFn	s_pfnOriginal;
LogOffFn			s_pfnOriginalLogOff;
Patch_t				s_Patches[ kMaxPatches ];
int					s_nPatches;

bool IsPatched( void **ppSlot )
{
	for ( int i = 0; i < s_nPatches; ++i )
	{
		if ( s_Patches[ i ].ppSlot == ppSlot )
		{
			return true;
		}
	}
	return false;
}

void InstallLogOffHook( void **pVTable )
{
	const int nSlot = steam::LogOffSlot();
	if ( nSlot < 0 )
	{
		return;
	}

	void **ppSlot = &pVTable[ nSlot ];
	if ( IsPatched( ppSlot ) )
	{
		return;
	}

	if ( s_nPatches >= kMaxPatches )
	{
		return;
	}

	LogOffFn pfnOriginal = (LogOffFn)*ppSlot;
	if ( s_pfnOriginalLogOff && pfnOriginal != s_pfnOriginalLogOff )
	{
		return;
	}

	void *pHook = (void *)&Hook_LogOff;
	if ( !plat::WriteMemory( ppSlot, &pHook, sizeof( pHook ) ) )
	{
		plat::Warn( "csgo-multi-appid: could not install the LogOff hook;"
					" the validator session will outlive the engine's at shutdown\n" );
		return;
	}

	s_pfnOriginalLogOff = pfnOriginal;
	s_Patches[ s_nPatches ].ppSlot = ppSlot;
	s_Patches[ s_nPatches ].pOriginal = (void *)pfnOriginal;
	++s_nPatches;
}

bool InstallHook( steam::ISteamGameServer *pServer )
{
	if ( !pServer )
		return false;

	const int nSlot = steam::BeginAuthSessionSlot();
	if ( nSlot < 0 )
	{
		s_bBroken = true;
		return false;
	}

	void **pVTable = *(void ***)pServer;
	void **ppSlot = &pVTable[ nSlot ];

	if ( IsPatched( ppSlot ) )
	{
		InstallLogOffHook( pVTable );
		return true;
	}

	if ( s_nPatches >= kMaxPatches )
		return false;

	// Every patched vtable has to forward to the same original, so a second
	// distinct vtable is only safe if its slot holds the same function.
	BeginAuthSessionFn pfnOriginal = (BeginAuthSessionFn)*ppSlot;
	if ( s_nPatches && pfnOriginal != s_pfnOriginal )
	{
		plat::Warn( "csgo-multi-appid: a second ISteamGameServer vtable has a different"
					" BeginAuthSession; leaving it alone\n" );
		return false;
	}

	void *pHook = (void *)&Hook_BeginAuthSession;
	if ( !plat::WriteMemory( ppSlot, &pHook, sizeof( pHook ) ) )
	{
		plat::Warn( "csgo-multi-appid: could not install the BeginAuthSession hook;"
					" clients from appid %u will keep being rejected\n", appid::Other() );
		s_bBroken = true;
		return false;
	}

	s_pfnOriginal = pfnOriginal;
	s_Patches[ s_nPatches ].ppSlot = ppSlot;
	s_Patches[ s_nPatches ].pOriginal = (void *)pfnOriginal;
	++s_nPatches;

	InstallLogOffHook( pVTable );
	return true;
}

void StopValidator()
{
	if ( !s_bValidatorStarted )
	{
		return;
	}

	validator::Stop();
	s_bValidatorStarted = false;
}

// Everything the feature needs, done as early as it can be done. Nothing here
// waits on the engine's own Steam session: the validator builds its own, and
// the vtable being patched belongs to steamclient's adapter class rather than
// to any one instance of it.
void Setup()
{
	if ( s_bBroken )
		return;

	if ( !s_bValidatorStarted )
	{
		if ( !validator::Start() )
			return;
		s_bValidatorStarted = true;
	}

	InstallHook( (steam::ISteamGameServer *)validator::Interface() );

	// If the engine has a session of its own by now, and it turns out not to
	// share a vtable with ours, this catches the other one.
	if ( steam::ISteamGameServer *pEngine = (steam::ISteamGameServer *)validator::EngineInterface() )
		InstallHook( pEngine );
}

steam::EBeginAuthSessionResult HOOK_CALL Hook_BeginAuthSession( HOOK_THIS, const void *pTicket, int cbTicket, uint64_t steamID )
{
	const steam::EBeginAuthSessionResult result = s_pfnOriginal( HOOK_FORWARD, pTicket, cbTicket, steamID );

	// The validator's own call, if it shares a vtable with the engine's
	// interface. Diverting it into itself is how this would recurse.
	if ( validator::IsOwnInterface( pThis ) )
		return result;

	// A hibernating server runs no frames, so this is the only chance to drain
	// the validator's callbacks before the answer below is needed.
	validator::Pump();

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

void HOOK_CALL Hook_LogOff( HOOK_THIS )
{
	const bool bOwn = validator::IsOwnInterface( pThis );
	if ( !bOwn )
	{
		StopValidator();
	}

	s_pfnOriginalLogOff( HOOK_FORWARD );
}

} // namespace

void Init()
{
	plat::Log( "csgo-multi-appid: clients whose tickets are for appid %u will be validated separately\n",
			   appid::Other() );

	// Deliberately not left to the first frame. An empty server hibernates, and
	// SV_Think returns before it reaches g_pServerPluginHandler->GameFrame, so
	// on a server nobody has joined yet GameFrame may never run at all -- and
	// that is exactly the server a client is about to connect to.
	Setup();
}

void Tick()
{
	if ( s_bBroken )
		return;

	// Frames are a convenience here, not the mechanism: Setup() has usually run
	// to completion during Load(). This retries whatever did not take, and is
	// also where the engine's interface gets picked up if it does not share a
	// vtable with the validator's.
	Setup();

	validator::Pump();
}

void Shutdown()
{
	// Unhook first: the validator's interface goes away with it, and a vtable
	// still pointing at this module would be a crash waiting to happen.
	for ( int i = 0; i < s_nPatches; ++i )
		plat::WriteMemory( s_Patches[ i ].ppSlot, &s_Patches[ i ].pOriginal, sizeof( void * ) );
	s_nPatches = 0;

	StopValidator();
}

} // namespace authproxy
