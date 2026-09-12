// Self-check for plat::FindUnique. The plugin writes to engine memory based on
// what this returns, so "matched the wrong thing" and "matched twice" have to
// behave the way appid.cpp assumes: return null rather than guess.

#include "platform.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{

// The real Windows signature, and the bytes it is meant to match:
//   mov esi, eax / add esp, 8 / test esi, esi / jz +0x47 / push [g_unSteamAppID]
const char *const kWinSig = "8B F0 83 C4 08 85 F6 74 ?? FF 35";
const unsigned char kWinBytes[] = {
	0x8B, 0xF0, 0x83, 0xC4, 0x08, 0x85, 0xF6, 0x74, 0x47, 0xFF, 0x35,
	0x50, 0x69, 0x92, 0x13,
};
const int kWinOperand = 11;

int g_nFailures;

void Check( bool bCondition, const char *pszWhat )
{
	printf( "%-58s %s\n", pszWhat, bCondition ? "ok" : "FAILED" );
	if ( !bCondition )
		++g_nFailures;
}

} // namespace

int main()
{
	unsigned char haystack[ 512 ];
	memset( haystack, 0x90, sizeof( haystack ) );

	// Absent.
	Check( plat::FindUnique( haystack, sizeof( haystack ), kWinSig ) == nullptr,
		   "no match returns null" );

	// Present once, wildcard covers the jz displacement.
	memcpy( haystack + 100, kWinBytes, sizeof( kWinBytes ) );
	const unsigned char *pHit = plat::FindUnique( haystack, sizeof( haystack ), kWinSig );
	Check( pHit == haystack + 100, "single match returns the match" );

	if ( pHit )
	{
		uint32_t nOperand = 0;
		memcpy( &nOperand, pHit + kWinOperand, sizeof( nOperand ) );
		Check( nOperand == 0x13926950u, "operand is read from the right offset" );
	}

	// A different displacement still matches: that byte is wildcarded.
	haystack[ 100 + 8 ] = 0x11;
	Check( plat::FindUnique( haystack, sizeof( haystack ), kWinSig ) == haystack + 100,
		   "wildcard byte is not compared" );

	// Present twice: ambiguous, so refuse.
	memcpy( haystack + 300, kWinBytes, sizeof( kWinBytes ) );
	Check( plat::FindUnique( haystack, sizeof( haystack ), kWinSig ) == nullptr,
		   "duplicate match returns null" );

	// A malformed pattern must not match anything.
	Check( plat::FindUnique( haystack, sizeof( haystack ), "8B ZZ" ) == nullptr,
		   "malformed pattern returns null" );
	Check( plat::FindUnique( haystack, sizeof( haystack ), "" ) == nullptr,
		   "empty pattern returns null" );

	printf( "\n%s\n", g_nFailures ? "FAILURES" : "all checks passed" );
	return g_nFailures ? 1 : 0;
}
