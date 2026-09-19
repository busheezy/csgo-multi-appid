// Minimal Steamworks mirrors.
//
// A Steam interface's layout is pinned by the version string you ask for, so
// "SteamClient017" means the same vtable everywhere and mirroring one is safe
// in a way that mirroring an engine interface is not. What is not safe is
// assuming which version the engine asks for -- see ISteamGameServer below.
// Only the slots this project calls carry real signatures; the rest are padding
// that exists to put those methods at the right index.
#pragma once

#include <cstdint>

namespace steam
{

typedef int32_t HSteamPipe;
typedef int32_t HSteamUser;

const char *const kSteamClientVersion = "SteamClient017";

// The version of ISteamGameServer is not this plugin's to choose. steamclient
// hands out a separate adapter object per version, each with its own vtable, so
// asking for a version other than the one the engine asked for gets a different
// object, and hooking that one changes nothing because the engine never calls
// it. The slot BeginAuthSession sits at moves between versions too, so a
// mismatch is not merely useless but dangerous: the write lands on whichever
// method occupies that index instead.
//
// So the version is read out of the engine binary rather than assumed, and the
// slot is looked up beside it. A version whose slots have not been checked gets
// no hook at all.
//
// The mirror below is ISteamGameServer014, which is what the engine asks for.
// Its BeginAuthSession slot is verified two ways: CSteam3Server::
// NotifyClientConnect calls `call [edx+68h]` in engine.dll and `call [ecx+68h]`
// in engine.so, 0x68 / 4 = 26; and the SDK v1.52 header agrees, once allowance
// is made for it having dropped InitGameServer from the published interface
// without the implementation losing the slot -- every slot below is one higher
// than that header's.
// Whether the Steam modules this needs are loaded at all. False early on: the
// engine only loads steamclient when it activates its own game server session,
// which is at map load, long after plugins.
bool ModulesReady();

const char *GameServerVersion();
int BeginAuthSessionSlot();
int LogOffSlot();

const int kEAccountTypeGameServer = 3;

// isteamgameserver.h
const uint32_t kServerFlagSecure = 0x02;
const uint32_t kServerFlagDedicated = 0x04;

enum EBeginAuthSessionResult
{
	k_EBeginAuthSessionResultOK = 0,
	k_EBeginAuthSessionResultInvalidTicket = 1,
	k_EBeginAuthSessionResultDuplicateRequest = 2,
	k_EBeginAuthSessionResultInvalidVersion = 3,
	k_EBeginAuthSessionResultGameMismatch = 4,
	k_EBeginAuthSessionResultExpiredTicket = 5,
};

enum EAuthSessionResponse
{
	k_EAuthSessionResponseOK = 0,
	k_EAuthSessionResponseUserNotConnectedToSteam = 1,
	k_EAuthSessionResponseNoLicenseOrExpired = 2,
	k_EAuthSessionResponseVACBanned = 3,
	k_EAuthSessionResponseLoggedInElseWhere = 4,
	k_EAuthSessionResponseVACCheckTimedOut = 5,
	k_EAuthSessionResponseAuthTicketCanceled = 6,
	k_EAuthSessionResponseAuthTicketInvalidAlreadyUsed = 7,
	k_EAuthSessionResponseAuthTicketInvalid = 8,
	k_EAuthSessionResponsePublisherIssuedBan = 9,

	// Ours: no verdict was obtained at all.
	k_EAuthSessionResponseUnavailable = 100,
};

// Callback packing differs per platform; steamclientpublic.h picks pack(4) on
// Linux/macOS and pack(8) elsewhere.
#if defined( __linux__ ) || defined( __APPLE__ )
#pragma pack( push, 4 )
#else
#pragma pack( push, 8 )
#endif
struct ValidateAuthTicketResponse_t
{
	enum { k_iCallback = 100 + 43 }; // k_iSteamUserCallbacks + 43
	uint64_t				m_SteamID;
	EAuthSessionResponse	m_eAuthSessionResponse;
	uint64_t				m_OwnerSteamID;
};

struct CallbackMsg_t
{
	HSteamUser	m_hSteamUser;
	int			m_iCallback;
	uint8_t		*m_pubParam;
	int			m_cubParam;
};
#pragma pack( pop )

class ISteamGameServer
{
public:
	virtual bool	InitGameServer( uint32_t unIP, uint16_t usGamePort, uint16_t usQueryPort,
									uint32_t unFlags, uint32_t nGameAppId, const char *pchVersionString ) = 0; // 0
	virtual void	SetProduct( const char * ) = 0;						// 1
	virtual void	SetGameDescription( const char * ) = 0;				// 2
	virtual void	SetModDir( const char * ) = 0;						// 3
	virtual void	SetDedicatedServer( bool ) = 0;						// 4
	virtual void	_LogOn( const char * ) = 0;							// 5
	virtual void	LogOnAnonymous() = 0;								// 6
	virtual void	LogOff() = 0;										// 7
	virtual bool	BLoggedOn() = 0;									// 8
	virtual bool	_BSecure() = 0;										// 9
	virtual void	_GetSteamID() = 0;									// 10
	virtual void	_WasRestartRequested() = 0;							// 11
	virtual void	_SetMaxPlayerCount() = 0;							// 12
	virtual void	_SetBotPlayerCount() = 0;							// 13
	virtual void	_SetServerName() = 0;								// 14
	virtual void	_SetMapName() = 0;									// 15
	virtual void	_SetPasswordProtected() = 0;						// 16
	virtual void	_SetSpectatorPort() = 0;							// 17
	virtual void	_SetSpectatorServerName() = 0;						// 18
	virtual void	_ClearAllKeyValues() = 0;							// 19
	virtual void	_SetKeyValue() = 0;									// 20
	virtual void	_SetGameTags() = 0;									// 21
	virtual void	_SetGameData() = 0;									// 22
	virtual void	_SetRegion() = 0;									// 23
	virtual void	_SetAdvertiseServerActive() = 0;					// 24
	virtual void	_GetAuthSessionTicket() = 0;						// 25
	virtual EBeginAuthSessionResult BeginAuthSession( const void *pAuthTicket, int cbAuthTicket, uint64_t steamID ) = 0; // 26
	virtual void	EndAuthSession( uint64_t steamID ) = 0;				// 27
};

class ISteamClient
{
public:
	virtual HSteamPipe	CreateSteamPipe() = 0;							// 0
	virtual bool		BReleaseSteamPipe( HSteamPipe ) = 0;			// 1
	virtual void		_ConnectToGlobalUser() = 0;						// 2
	virtual HSteamUser	CreateLocalUser( HSteamPipe *phSteamPipe, int eAccountType ) = 0; // 3
	virtual void		ReleaseUser( HSteamPipe, HSteamUser ) = 0;		// 4
	virtual void		_GetISteamUser() = 0;							// 5
	virtual ISteamGameServer *GetISteamGameServer( HSteamUser, HSteamPipe, const char *pchVersion ) = 0; // 6
};

// Everything is resolved by name from modules Steam has already loaded, so
// nothing here links against steam_api.
struct Api
{
	HSteamUser ( *GetHSteamUser )() = nullptr;
	HSteamPipe ( *GetHSteamPipe )() = nullptr;

	// Per-pipe dispatch. steam_api's SteamGameServer_RunCallbacks() only ever
	// pumps the engine's pipe; these let us pump our own without touching it.
	bool ( *BGetCallback )( HSteamPipe, CallbackMsg_t * ) = nullptr;
	void ( *FreeLastCallback )( HSteamPipe ) = nullptr;

	bool Load();

	ISteamClient *Client();

	// The game server interface belonging to a pipe/user pair.
	ISteamGameServer *GameServer( HSteamUser hUser, HSteamPipe hPipe );

	// The session the engine created, if it exists yet.
	ISteamGameServer *EngineGameServer();
};

} // namespace steam
