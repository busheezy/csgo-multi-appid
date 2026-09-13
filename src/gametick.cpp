#include "gametick.h"
#include "platform.h"

namespace gametick
{
namespace
{

// Every shipped binary agrees on this one -- engine.dll, engine.so, server.dll
// and server.so all carry ServerGameDLL005, and the public header describes the
// same layout, so unlike ISteamGameServer there is no version drift to detect.
const char *const kServerGameDLLVersion = "ServerGameDLL005";

// IServerGameDLL::Think. Confirmed twice over: eiface.h puts Think at 30, and
// SV_Frame calls it as `call dword ptr [eax+78h]`, 0x78 / 4 = 30. The same
// vtable's GetGameDescription lands at 11 / 0x2C, which is what
// CSteam3Server::Activate calls, so the two line up.
const int kThinkSlot = 30;

#if defined( _WIN32 )
// A virtual is __thiscall; __fastcall with an unused edx gives a free function
// the same register layout.
#define TICK_CALL __fastcall
#define TICK_THIS void *pThis, void * /*edx*/
#define TICK_FORWARD pThis, nullptr
typedef void( TICK_CALL *ThinkFn )( void *, void *, bool );
#else
// Itanium ABI: this is simply the first argument.
#define TICK_CALL
#define TICK_THIS void *pThis
#define TICK_FORWARD pThis
typedef void( *ThinkFn )( void *, bool );
#endif

ThinkFn		s_pfnOriginal;
void		**s_ppSlot;
Callback	s_pfnTick;

void TICK_CALL Hook_Think( TICK_THIS, bool bFinalTick )
{
	// Ahead of the game DLL's own work, so a tick still happens even if that
	// returns early.
	if ( s_pfnTick )
		s_pfnTick();

	s_pfnOriginal( TICK_FORWARD, bFinalTick );
}

} // namespace

bool Install( CreateInterfaceFn gameServerFactory, Callback pfnTick )
{
	if ( s_ppSlot )
		return true;
	if ( !gameServerFactory || !pfnTick )
		return false;

	void *pGameDLL = gameServerFactory( kServerGameDLLVersion, nullptr );
	if ( !pGameDLL )
	{
		plat::Warn( "csgo-multi-appid: no %s from the game DLL;"
					" falling back to ticking on GameFrame\n", kServerGameDLLVersion );
		return false;
	}

	void **pVTable = *(void ***)pGameDLL;
	void **ppSlot = &pVTable[ kThinkSlot ];
	ThinkFn pfnOriginal = (ThinkFn)*ppSlot;
	if ( !pfnOriginal )
		return false;

	void *pHook = (void *)&Hook_Think;
	if ( !plat::WriteMemory( ppSlot, &pHook, sizeof( pHook ) ) )
	{
		plat::Warn( "csgo-multi-appid: could not hook %s::Think;"
					" falling back to ticking on GameFrame\n", kServerGameDLLVersion );
		return false;
	}

	s_pfnOriginal = pfnOriginal;
	s_ppSlot = ppSlot;
	s_pfnTick = pfnTick;
	return true;
}

bool Installed()
{
	return s_ppSlot != nullptr;
}

void Remove()
{
	if ( !s_ppSlot )
		return;

	void *pOriginal = (void *)s_pfnOriginal;
	plat::WriteMemory( s_ppSlot, &pOriginal, sizeof( pOriginal ) );
	s_ppSlot = nullptr;
	s_pfnTick = nullptr;
}

} // namespace gametick
