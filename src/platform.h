// OS abstraction. Everything the plugin needs that differs between the Windows
// and Linux dedicated servers lives here, so the actual logic stays readable.
#pragma once

#include <cstddef>
#include <cstdint>

namespace plat
{
// Routed through tier0's Msg/Warning when they can be resolved, stderr otherwise.
void Log( const char *pszFormat, ... );
void Warn( const char *pszFormat, ... );

// Executable range of a loaded module, named as the file is ("engine.dll", "engine.so").
bool ModuleTextRange( const char *pszModule, const unsigned char **ppStart, size_t *pSize );

// The module's whole mapped image rather than just its code. A pointer read out
// of an instruction operand is only a number until it is known to land in here,
// and dereferencing one that does not takes the server down.
bool ModuleImageRange( const char *pszModule, const unsigned char **ppStart, size_t *pSize );
bool ModuleLoaded( const char *pszModule );

// Pattern is hex bytes with "??" wildcards, e.g. "8B F0 ?? ?? 85 F6".
// Returns null unless exactly one match exists: an ambiguous pattern is a bug,
// not something to guess at.
const unsigned char *FindUnique( const unsigned char *pStart, size_t nSize, const char *pszPattern );

bool WriteMemory( void *pAddr, const void *pBytes, size_t nLen );

// A UDP port nothing is bound to right now, by binding one and letting go of it
// again. 0 if none could be had. Inherently a race, so the caller has to cope
// with the port being taken by the time it is used.
uint16_t FreeUdpPort();

void SetEnv( const char *pszKey, const char *pszValue );
}
