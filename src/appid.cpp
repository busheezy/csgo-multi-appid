#include "appid.h"
#include "platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace appid
{
namespace
{

#if defined( _WIN32 )
const char *const kEngineModule = "engine.dll";
const char *const kDedicatedModule = "dedicated.dll";

// CBaseFileSystem-adjacent startup code that writes steam_appid.txt from the
// parsed steam.inf value. The instruction after the null check pushes the
// global itself:
//
//   mov  esi, eax                    <- fopen result
//   add  esp, 8
//   test esi, esi
//   jz   short done
//   push dword ptr [g_unSteamAppID]  <- operand is what we want
const char *const kAppIdSig = "8B F0 83 C4 08 85 F6 74 ?? FF 35";
const int kAppIdOperand = 11;
#else
const char *const kEngineModule = "engine.so";
const char *const kDedicatedModule = "dedicated.so";

//   mov  ebx, eax                    <- fopen result
//   test eax, eax
//   jz   done
//   push ds:g_unSteamAppID           <- operand is what we want
//
// The push carries an absolute address that the loader relocates, so reading it
// at runtime already yields the mapped address.
const char *const kAppIdSig = "89 C3 85 C0 0F 84 ?? ?? ?? ?? FF 35";
const int kAppIdOperand = 12;
#endif

// Sanity bound for "this pointer really is an appid". Steam appids are well
// under this, and it catches a signature that matched the wrong thing.
const uint32_t kMaxPlausibleAppId = 50000000;

uint32_t *s_pEngineAppId;
uint32_t s_nOriginalValue;

uint32_t *FindEngineAppId()
{
	const unsigned char *pText = nullptr;
	size_t nSize = 0;
	if ( !plat::ModuleTextRange( kEngineModule, &pText, &nSize ) )
	{
		plat::Warn( "csgo-multi-appid: %s is not loaded\n", kEngineModule );
		return nullptr;
	}

	const unsigned char *pMatch = plat::FindUnique( pText, nSize, kAppIdSig );
	if ( !pMatch )
	{
		plat::Warn( "csgo-multi-appid: steam.inf appid pattern not found (or not unique) in %s;"
					" the advertised appid will keep following steam.inf\n", kEngineModule );
		return nullptr;
	}

	uint32_t nAddr = 0;
	memcpy( &nAddr, pMatch + kAppIdOperand, sizeof( nAddr ) );

	uint32_t *pGlobal = (uint32_t *)(uintptr_t)nAddr;
	if ( !pGlobal || *pGlobal >= kMaxPlausibleAppId )
	{
		plat::Warn( "csgo-multi-appid: value at 0x%p is not a plausible appid, leaving it alone\n", (void *)pGlobal );
		return nullptr;
	}
	return pGlobal;
}

bool WriteSteamAppIdFile( uint32_t nAppId )
{
	// Same relative path and format the engine itself uses, so steamclient finds
	// it regardless of which of us wrote it last.
	FILE *f = fopen( "steam_appid.txt", "wb" );
	if ( !f )
	{
		plat::Warn( "csgo-multi-appid: could not write steam_appid.txt (working directory not writable?)\n" );
		return false;
	}

	fprintf( f, "%u\n", nAppId );
	fclose( f );
	return true;
}

} // namespace

bool IsDedicatedServer()
{
	return plat::ModuleLoaded( kDedicatedModule );
}

void Apply()
{
	if ( !IsDedicatedServer() )
	{
		// steam_appid.txt in a client's game directory would change what the
		// client itself launches as.
		plat::Log( "csgo-multi-appid: not a dedicated server, doing nothing\n" );
		return;
	}

	const uint32_t nTarget = Pinned();

	char szTarget[ 32 ];
	snprintf( szTarget, sizeof( szTarget ), "%u", nTarget );

	WriteSteamAppIdFile( nTarget );
	plat::SetEnv( "SteamAppId", szTarget );

	s_pEngineAppId = FindEngineAppId();
	if ( s_pEngineAppId )
	{
		s_nOriginalValue = *s_pEngineAppId;
		if ( s_nOriginalValue != nTarget )
			plat::WriteMemory( s_pEngineAppId, &nTarget, sizeof( nTarget ) );
	}

	if ( s_pEngineAppId && s_nOriginalValue != nTarget )
	{
		plat::Log( "csgo-multi-appid: appid pinned to %u (steam.inf said %u)\n", nTarget, s_nOriginalValue );
	}
	else
	{
		plat::Log( "csgo-multi-appid: appid pinned to %u\n", nTarget );
	}
	plat::Log( "csgo-multi-appid: the Steam logon picks this up when the server activates;"
			   " restart the server if it is already logged on\n" );
}

void Restore()
{
	if ( !s_pEngineAppId )
		return;

	plat::WriteMemory( s_pEngineAppId, &s_nOriginalValue, sizeof( s_nOriginalValue ) );
	s_pEngineAppId = nullptr;
}

} // namespace appid
