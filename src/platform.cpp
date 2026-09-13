#include "platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace plat
{
namespace
{

typedef void ( *SpewFn )( const char *pszFormat, ... );

SpewFn s_pfnMsg;
SpewFn s_pfnWarning;
bool s_bSpewResolved;

void ResolveSpew()
{
	if ( s_bSpewResolved )
		return;
	s_bSpewResolved = true;

#if defined( _WIN32 )
	if ( HMODULE h = GetModuleHandleA( "tier0.dll" ) )
	{
		s_pfnMsg = (SpewFn)GetProcAddress( h, "Msg" );
		s_pfnWarning = (SpewFn)GetProcAddress( h, "Warning" );
	}
#else
	// tier0 may have been loaded RTLD_LOCAL, in which case RTLD_DEFAULT misses it.
	void *pTier0 = dlopen( "libtier0.so", RTLD_NOLOAD | RTLD_NOW );
	s_pfnMsg = (SpewFn)dlsym( pTier0 ? pTier0 : RTLD_DEFAULT, "Msg" );
	s_pfnWarning = (SpewFn)dlsym( pTier0 ? pTier0 : RTLD_DEFAULT, "Warning" );
#endif
}

void Spew( bool bWarning, const char *pszFormat, va_list args )
{
	char buf[ 1024 ];
	vsnprintf( buf, sizeof( buf ), pszFormat, args );
	buf[ sizeof( buf ) - 1 ] = '\0';

	ResolveSpew();
	SpewFn pfn = bWarning ? s_pfnWarning : s_pfnMsg;
	if ( !pfn )
		pfn = bWarning ? s_pfnMsg : s_pfnWarning;

	if ( pfn )
		pfn( "%s", buf );
	else
		fputs( buf, stderr );
}

int HexDigit( char c )
{
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

#if !defined( _WIN32 )
struct ModuleRange_t
{
	const char		*pszWanted;
	unsigned char	*pStart;
	size_t			nSize;
	bool			bFound;
};

const char *BaseName( const char *pszPath )
{
	const char *pSlash = strrchr( pszPath, '/' );
	return pSlash ? pSlash + 1 : pszPath;
}

int ModuleCallback( struct dl_phdr_info *pInfo, size_t, void *pUser )
{
	ModuleRange_t *pQuery = (ModuleRange_t *)pUser;
	if ( !pInfo->dlpi_name || !*pInfo->dlpi_name )
		return 0;
	if ( strcmp( BaseName( pInfo->dlpi_name ), pQuery->pszWanted ) != 0 )
		return 0;

	for ( int i = 0; i < pInfo->dlpi_phnum; ++i )
	{
		const ElfW( Phdr ) &phdr = pInfo->dlpi_phdr[ i ];
		if ( phdr.p_type != PT_LOAD || !( phdr.p_flags & PF_X ) )
			continue;

		pQuery->pStart = (unsigned char *)( pInfo->dlpi_addr + phdr.p_vaddr );
		pQuery->nSize = phdr.p_memsz;
		pQuery->bFound = true;
		return 1;
	}
	return 0;
}
#endif

} // namespace

void Log( const char *pszFormat, ... )
{
	va_list args;
	va_start( args, pszFormat );
	Spew( false, pszFormat, args );
	va_end( args );
}

void Warn( const char *pszFormat, ... )
{
	va_list args;
	va_start( args, pszFormat );
	Spew( true, pszFormat, args );
	va_end( args );
}

bool ModuleTextRange( const char *pszModule, const unsigned char **ppStart, size_t *pSize )
{
#if defined( _WIN32 )
	HMODULE h = GetModuleHandleA( pszModule );
	if ( !h )
		return false;

	unsigned char *pBase = (unsigned char *)h;
	IMAGE_DOS_HEADER *pDos = (IMAGE_DOS_HEADER *)pBase;
	IMAGE_NT_HEADERS *pNt = (IMAGE_NT_HEADERS *)( pBase + pDos->e_lfanew );
	IMAGE_SECTION_HEADER *pSec = IMAGE_FIRST_SECTION( pNt );

	for ( int i = 0; i < pNt->FileHeader.NumberOfSections; ++i )
	{
		if ( pSec[ i ].Characteristics & IMAGE_SCN_MEM_EXECUTE )
		{
			*ppStart = pBase + pSec[ i ].VirtualAddress;
			*pSize = pSec[ i ].Misc.VirtualSize;
			return true;
		}
	}
	return false;
#else
	ModuleRange_t query = { pszModule, nullptr, 0, false };
	dl_iterate_phdr( ModuleCallback, &query );
	if ( !query.bFound )
		return false;

	*ppStart = query.pStart;
	*pSize = query.nSize;
	return true;
#endif
}

bool ModuleLoaded( const char *pszModule )
{
	const unsigned char *pStart = nullptr;
	size_t nSize = 0;
	return ModuleTextRange( pszModule, &pStart, &nSize );
}

const unsigned char *FindUnique( const unsigned char *pStart, size_t nSize, const char *pszPattern )
{
	unsigned char sig[ 64 ];
	bool wild[ 64 ];
	int nSigLen = 0;

	for ( const char *p = pszPattern; *p && nSigLen < (int)sizeof( sig ); )
	{
		if ( *p == ' ' )
		{
			++p;
			continue;
		}
		if ( *p == '?' )
		{
			sig[ nSigLen ] = 0;
			wild[ nSigLen ] = true;
			++nSigLen;
			p += ( p[ 1 ] == '?' ) ? 2 : 1;
			continue;
		}

		int hi = HexDigit( p[ 0 ] );
		int lo = HexDigit( p[ 1 ] );
		if ( hi < 0 || lo < 0 )
			return nullptr;

		sig[ nSigLen ] = (unsigned char)( ( hi << 4 ) | lo );
		wild[ nSigLen ] = false;
		++nSigLen;
		p += 2;
	}

	if ( nSigLen == 0 )
		return nullptr;

	const unsigned char *pFound = nullptr;
	for ( size_t i = 0; i + nSigLen <= nSize; ++i )
	{
		int j = 0;
		for ( ; j < nSigLen; ++j )
		{
			if ( !wild[ j ] && pStart[ i + j ] != sig[ j ] )
				break;
		}
		if ( j != nSigLen )
			continue;

		if ( pFound )
			return nullptr; // ambiguous
		pFound = pStart + i;
	}
	return pFound;
}

bool WriteMemory( void *pAddr, const void *pBytes, size_t nLen )
{
#if defined( _WIN32 )
	DWORD old = 0;
	if ( !VirtualProtect( pAddr, nLen, PAGE_EXECUTE_READWRITE, &old ) )
		return false;
	memcpy( pAddr, pBytes, nLen );
	VirtualProtect( pAddr, nLen, old, &old );
	return true;
#else
	long nPageSize = sysconf( _SC_PAGESIZE );
	uintptr_t addr = (uintptr_t)pAddr;
	uintptr_t page = addr & ~( (uintptr_t)nPageSize - 1 );
	size_t nSpan = ( addr + nLen ) - page;

	if ( mprotect( (void *)page, nSpan, PROT_READ | PROT_WRITE | PROT_EXEC ) != 0 )
		return false;
	memcpy( pAddr, pBytes, nLen );
	return true;
#endif
}

uint16_t FreeUdpPort()
{
#if defined( _WIN32 )
	SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( s == INVALID_SOCKET )
		return 0;
	int nLen = sizeof( sockaddr_in );
#else
	int s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if ( s < 0 )
		return 0;
	socklen_t nLen = sizeof( sockaddr_in );
#endif

	sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = 0; // the OS picks

	uint16_t nPort = 0;
	if ( bind( s, (sockaddr *)&addr, sizeof( addr ) ) == 0 &&
		 getsockname( s, (sockaddr *)&addr, &nLen ) == 0 )
	{
		nPort = ntohs( addr.sin_port );
	}

#if defined( _WIN32 )
	closesocket( s );
#else
	close( s );
#endif
	return nPort;
}

void SetEnv( const char *pszKey, const char *pszValue )
{
#if defined( _WIN32 )
	SetEnvironmentVariableA( pszKey, pszValue );
	// The CRT keeps its own copy, and steam_api reads through getenv().
	char buf[ 256 ];
	snprintf( buf, sizeof( buf ), "%s=%s", pszKey, pszValue );
	_putenv( buf );
#else
	setenv( pszKey, pszValue, 1 );
#endif
}

} // namespace plat
