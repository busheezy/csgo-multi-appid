#include "workshop.h"
#include "appid.h"
#include "platform.h"

#include <cstdint>
#include <cstring>

namespace workshop
{
namespace
{

// DedicatedServerUGCFileInfo_t::BuildFromKV, at the consumer_appid check:
//
//   Windows                              Linux
//   mov  ecx, dword_<engine>             mov  edi, eax          ; item appid
//   mov  ebx, eax        ; item appid    mov  eax, ds:<engine>
//   mov  edx, [ecx]                      mov  edx, [eax]
//   call [edx+19Ch]      ; GetAppID      mov  [esp], eax
//   cmp  ebx, eax                        call [edx+19Ch]        ; GetAppID
//   jz   short ok                        add  esp, 10h
//                                        cmp  edi, eax
//                                        jz   short ok
//
// Everything from the start of the match up to the cmp is replaced: the engine
// call goes away and eax is set to whichever of the two appids the item claims,
// so the original cmp/jz passes for both and fails for anything else.
//
// The Linux sequence folds the argument cleanup for two calls into one
// `add esp, 10h`, so the replacement has to keep that.

#if defined( _WIN32 )
const char *const kServerModule = "server.dll";
const char *const kCheckSig = "8B 0D ?? ?? ?? ?? 8B D8 8B 11 FF 92 9C 01 00 00 3B D8 74";
const size_t kPatchLength = 16;
#else
const char *const kServerModule = "server.so";
const char *const kServerModuleAlt = "server_srv.so";
const char *const kCheckSig = "89 C7 A1 ?? ?? ?? ?? 8B 10 89 04 24 FF 92 9C 01 00 00 83 C4 10 39 C7 74";
const size_t kPatchLength = 21;
#endif

const size_t kMaxPatch = 32;

unsigned char	*s_pPatchSite;
unsigned char	s_Original[ kMaxPatch ];

size_t Emit( unsigned char *pOut, size_t n, unsigned char nOpcode, uint32_t nImm )
{
	pOut[ n++ ] = nOpcode;
	memcpy( pOut + n, &nImm, sizeof( nImm ) );
	return n + sizeof( nImm );
}

// eax holds the item's consumer_appid on entry, and has to hold a value the
// following cmp will match if that appid is one of ours.
size_t BuildReplacement( unsigned char *pOut )
{
	size_t n = 0;

#if defined( _WIN32 )
	pOut[ n++ ] = 0x8B;								// mov ebx, eax
	pOut[ n++ ] = 0xD8;
	n = Emit( pOut, n, 0xB8, appid::Pinned() );		// mov eax, <pinned>
	pOut[ n++ ] = 0x3B;								// cmp ebx, eax
	pOut[ n++ ] = 0xD8;
	pOut[ n++ ] = 0x74;								// jz +5
	pOut[ n++ ] = 0x05;
	n = Emit( pOut, n, 0xB8, appid::Other() );		// mov eax, <other>
#else
	pOut[ n++ ] = 0x89;								// mov edi, eax
	pOut[ n++ ] = 0xC7;
	pOut[ n++ ] = 0x83;								// add esp, 10h
	pOut[ n++ ] = 0xC4;
	pOut[ n++ ] = 0x10;
	n = Emit( pOut, n, 0xB8, appid::Pinned() );		// mov eax, <pinned>
	pOut[ n++ ] = 0x39;								// cmp edi, eax
	pOut[ n++ ] = 0xC7;
	pOut[ n++ ] = 0x74;								// jz +5
	pOut[ n++ ] = 0x05;
	n = Emit( pOut, n, 0xB8, appid::Other() );		// mov eax, <other>
	pOut[ n++ ] = 0x90;								// pad out to the cmp
	pOut[ n++ ] = 0x90;
#endif

	return n;
}

const unsigned char *FindCheck( const char **ppModule )
{
	const char *const candidates[] = {
		kServerModule,
#if !defined( _WIN32 )
		kServerModuleAlt,
#endif
	};

	for ( const char *pszModule : candidates )
	{
		const unsigned char *pText = nullptr;
		size_t nSize = 0;
		if ( !plat::ModuleTextRange( pszModule, &pText, &nSize ) )
			continue;

		*ppModule = pszModule;
		return plat::FindUnique( pText, nSize, kCheckSig );
	}

	*ppModule = nullptr;
	return nullptr;
}

} // namespace

void Apply()
{
	if ( !appid::IsDedicatedServer() )
		return;

	const char *pszModule = nullptr;
	const unsigned char *pMatch = FindCheck( &pszModule );

	if ( !pszModule )
	{
		plat::Warn( "csgo-multi-appid: the game DLL is not loaded; host_workshop_map left as it is\n" );
		return;
	}

	if ( !pMatch )
	{
		plat::Warn( "csgo-multi-appid: the consumer_appid check was not found (or not unique) in %s;"
					" host_workshop_map will keep rejecting items published under appid %u\n",
					pszModule, appid::Other() );
		return;
	}

	unsigned char patch[ kMaxPatch ];
	const size_t nPatch = BuildReplacement( patch );
	if ( nPatch > kPatchLength )
		return; // unreachable, and a compile-time mistake if it ever is not

	// Pad any slack with nops so the instruction the cmp follows stays put.
	memset( patch + nPatch, 0x90, kPatchLength - nPatch );

	unsigned char *pSite = const_cast< unsigned char * >( pMatch );
	memcpy( s_Original, pSite, kPatchLength );

	if ( !plat::WriteMemory( pSite, patch, kPatchLength ) )
	{
		plat::Warn( "csgo-multi-appid: could not patch the consumer_appid check in %s\n", pszModule );
		return;
	}

	s_pPatchSite = pSite;
	plat::Log( "csgo-multi-appid: host_workshop_map now accepts items from appid %u and %u\n",
			   appid::Pinned(), appid::Other() );
}

void Restore()
{
	if ( !s_pPatchSite )
		return;

	plat::WriteMemory( s_pPatchSite, s_Original, kPatchLength );
	s_pPatchSite = nullptr;
}

} // namespace workshop
