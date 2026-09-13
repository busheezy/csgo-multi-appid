#include "enginefwd.h"
#include "platform.h"
#include "steam_min.h"

#include <cstring>

namespace enginefwd
{
namespace
{

#if defined( _WIN32 )
const char *const kEngineModule = "engine.dll";

// CSteam3Server::OnValidateAuthTicketResponse prologue.
//   push ebp / mov ebp,esp / and esp,-8 / sub esp,224h / push ebx / push esi / mov esi,ecx
const char *const kHandlerSig = "55 8B EC 83 E4 F8 81 EC 24 02 00 00 53 56 8B F1";

// The singleton is inlined as an immediate at the NotifyClientConnect call site:
//   mov ecx, offset s_Steam3Server / push eax / push [ebp+arg_4] / push [ebp+arg_0] / call
const char *const kSingletonSig = "B9 ?? ?? ?? ?? 50 FF 75 0C FF 75 08 E8";
const int kSingletonOperand = 1;

// m_eServerMode, used to sanity check the candidate.
const int kServerModeOffset = 0x84;

typedef void( __fastcall *OnValidateFn )( void *pThis, void *pEdx, void *pResponse );
#else
const char *const kEngineModule = "engine.so";

//   push ebp / mov ebp,esp / push edi / mov edi,eax / push esi / mov esi,edx / push ebx / sub esp,244h
const char *const kHandlerSig = "55 89 E5 57 89 C7 56 89 D6 53 81 EC 44 02 00 00";

// Steam3Server() is a getter returning the singleton:
//   push ebp / mov eax, offset s_Steam3Server / mov ebp,esp / pop ebp / ret
// That shape is shared by hundreds of getters, so the candidate is confirmed by
// the m_eServerMode check below rather than by the pattern alone.
const char *const kSingletonSig = "55 B8 ?? ?? ?? ?? 89 E5 5D C3";
const int kSingletonOperand = 2;

const int kServerModeOffset = 0x98;

// The handler takes this in eax and the response in edx.
typedef void( __attribute__( ( regparm( 3 ) ) ) *OnValidateFn )( void *pThis, void *pResponse );
#endif

OnValidateFn		s_pfnOnValidate;
void				*s_pSteam3Server;
bool				s_bTried;
const unsigned char	*s_pImage;
size_t				s_nImageSize;

// A candidate address comes out of an instruction operand, so it is a number
// and nothing more until it is known to land inside the engine's own mapped
// image. Most of the sequences the Linux scan below matches are not the getter
// being looked for, and their operands are not addresses at all: reading
// through one takes the server down with SIGSEGV.
bool InEngineImage( const void *p, size_t nLen )
{
	const unsigned char *pAddr = (const unsigned char *)p;
	return s_pImage && pAddr >= s_pImage && pAddr + nLen <= s_pImage + s_nImageSize;
}

bool LooksLikeSteam3Server( const void *p )
{
	if ( !p || !InEngineImage( p, kServerModeOffset + sizeof( int ) ) )
		return false;

	// eServerModeNoAuthentication .. eServerModeAuthenticationAndSecure
	const int nMode = *(const int *)( (const unsigned char *)p + kServerModeOffset );
	return nMode >= 1 && nMode <= 3;
}

} // namespace

bool Init()
{
	if ( s_bTried )
		return Available();
	s_bTried = true;

	const unsigned char *pText = nullptr;
	size_t nSize = 0;
	if ( !plat::ModuleTextRange( kEngineModule, &pText, &nSize ) )
		return false;

	// Needed before any candidate is looked at, not after.
	if ( !plat::ModuleImageRange( kEngineModule, &s_pImage, &s_nImageSize ) )
		return false;

	const unsigned char *pHandler = plat::FindUnique( pText, nSize, kHandlerSig );
	if ( !pHandler )
	{
		plat::Warn( "csgo-multi-appid: auth response handler not found;"
					" cross-appid verdicts can only be logged\n" );
		return false;
	}

#if defined( _WIN32 )
	// One unambiguous site carries the singleton as an immediate.
	if ( const unsigned char *pSite = plat::FindUnique( pText, nSize, kSingletonSig ) )
	{
		uint32_t nAddr = 0;
		memcpy( &nAddr, pSite + kSingletonOperand, sizeof( nAddr ) );
		if ( LooksLikeSteam3Server( (void *)(uintptr_t)nAddr ) )
			s_pSteam3Server = (void *)(uintptr_t)nAddr;
	}
#else
	// Many getters share the shape, so walk them and keep the one whose target
	// actually is a CSteam3Server.
	for ( size_t i = 0; i + 10 <= nSize && !s_pSteam3Server; ++i )
	{
		if ( pText[ i ] != 0x55 || pText[ i + 1 ] != 0xB8 ||
			 pText[ i + 6 ] != 0x89 || pText[ i + 7 ] != 0xE5 ||
			 pText[ i + 8 ] != 0x5D || pText[ i + 9 ] != 0xC3 )
			continue;

		uint32_t nAddr = 0;
		memcpy( &nAddr, pText + i + kSingletonOperand, sizeof( nAddr ) );
		if ( LooksLikeSteam3Server( (void *)(uintptr_t)nAddr ) )
			s_pSteam3Server = (void *)(uintptr_t)nAddr;
	}
#endif

	if ( !s_pSteam3Server )
	{
		plat::Warn( "csgo-multi-appid: CSteam3Server not located;"
					" cross-appid verdicts can only be logged\n" );
		return false;
	}

	s_pfnOnValidate = (OnValidateFn)pHandler;
	plat::Log( "csgo-multi-appid: verdicts will be handed to the engine's auth handler\n" );
	return true;
}

bool Available()
{
	return s_pfnOnValidate && s_pSteam3Server;
}

bool Forward( uint64_t steamID, int eAuthSessionResponse, uint64_t ownerSteamID )
{
	if ( !Available() )
		return false;

	steam::ValidateAuthTicketResponse_t response;
	memset( &response, 0, sizeof( response ) );
	response.m_SteamID = steamID;
	response.m_eAuthSessionResponse = (steam::EAuthSessionResponse)eAuthSessionResponse;
	response.m_OwnerSteamID = ownerSteamID ? ownerSteamID : steamID;

#if defined( _WIN32 )
	s_pfnOnValidate( s_pSteam3Server, nullptr, &response );
#else
	s_pfnOnValidate( s_pSteam3Server, &response );
#endif
	return true;
}

} // namespace enginefwd
