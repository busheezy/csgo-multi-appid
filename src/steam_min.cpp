#include "steam_min.h"
#include "platform.h"

#include <cstring>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace steam
{
namespace
{

typedef void *( *CreateInterfaceFn )( const char *pName, int *pReturnCode );

#if defined( _WIN32 )
const char *const kSteamApiModule = "steam_api.dll";
const char *const kSteamClientModule = "steamclient.dll";
#else
const char *const kSteamApiModule = "libsteam_api.so";
const char *const kSteamClientModule = "steamclient.so";
#endif

void *s_hSteamApi;
void *s_hSteamClient;

void *OpenLoaded( const char *pszName )
{
#if defined( _WIN32 )
	return (void *)GetModuleHandleA( pszName );
#else
	return dlopen( pszName, RTLD_NOW | RTLD_NOLOAD );
#endif
}

void *Symbol( void *hModule, const char *pszName )
{
	if ( !hModule )
		return nullptr;
#if defined( _WIN32 )
	return (void *)GetProcAddress( (HMODULE)hModule, pszName );
#else
	return dlsym( hModule, pszName );
#endif
}

// Steam_BGetCallback and friends live in steamclient on Windows and in both
// modules on Linux, so try each.
void *DispatchSymbol( const char *pszName )
{
	if ( void *p = Symbol( s_hSteamApi, pszName ) )
		return p;
	return Symbol( s_hSteamClient, pszName );
}

} // namespace

bool Api::Load()
{
	if ( !s_hSteamApi )
		s_hSteamApi = OpenLoaded( kSteamApiModule );
	if ( !s_hSteamClient )
		s_hSteamClient = OpenLoaded( kSteamClientModule );

	if ( !s_hSteamApi && !s_hSteamClient )
		return false; // Steam is not up in this process yet

	GetHSteamUser = (HSteamUser( * )())Symbol( s_hSteamApi, "SteamGameServer_GetHSteamUser" );
	GetHSteamPipe = (HSteamPipe( * )())Symbol( s_hSteamApi, "SteamGameServer_GetHSteamPipe" );
	BGetCallback = (bool ( * )( HSteamPipe, CallbackMsg_t * ))DispatchSymbol( "Steam_BGetCallback" );
	FreeLastCallback = (void ( * )( HSteamPipe ))DispatchSymbol( "Steam_FreeLastCallback" );

	return GetHSteamUser && GetHSteamPipe;
}

ISteamClient *Api::Client()
{
	if ( !s_hSteamClient )
		s_hSteamClient = OpenLoaded( kSteamClientModule );
	if ( !s_hSteamClient )
		return nullptr;

	CreateInterfaceFn pfnCreate = (CreateInterfaceFn)Symbol( s_hSteamClient, "CreateInterface" );
	if ( !pfnCreate )
		return nullptr;

	// Asking for the exact version keeps this vtable matching the mirror above.
	ISteamClient *pClient = (ISteamClient *)pfnCreate( kSteamClientVersion, nullptr );
	if ( !pClient )
		plat::Warn( "steam: %s not available\n", kSteamClientVersion );

	return pClient;
}

ISteamGameServer *Api::GameServer( HSteamUser hUser, HSteamPipe hPipe )
{
	if ( !hUser || !hPipe )
		return nullptr;

	ISteamClient *pClient = Client();
	return pClient ? pClient->GetISteamGameServer( hUser, hPipe, kSteamGameServerVersion ) : nullptr;
}

ISteamGameServer *Api::EngineGameServer()
{
	if ( !GetHSteamUser || !GetHSteamPipe )
		return nullptr;
	return GameServer( GetHSteamUser(), GetHSteamPipe() );
}

} // namespace steam
