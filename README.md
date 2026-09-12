# csgo-multi-appid

A Valve server plugin that pins the CS:GO dedicated server's Steam appid, so the
server behaves identically no matter what `csgo/steam.inf` says.

## Why

The legacy CS:GO build is distributed under appid **4465480**, but the depot was
re-published unchanged, so the shipped `csgo/steam.inf` still reads `AppID=730`.
That one line decides a surprising amount:

- which app the server logs on to Steam as (and therefore which GSLT is valid,
  and which clients' auth tickets validate instead of failing with
  `k_EBeginAuthSessionResultGameMismatch`)
- what `engine->GetAppID()` returns, which the workshop code compares against
  every item's `consumer_appid`
- the appid advertised in `A2S_INFO`, i.e. how the server is categorised in the
  server browser

So two otherwise identical installs behave differently depending on whether
someone remembered to edit `steam.inf` — and a `steamcmd ... validate` silently
reverts the edit. This plugin removes the file from the decision.

## What it does

On load, before the server logs on to Steam:

1. writes `steam_appid.txt` with the target appid, which is what `steamclient`
   reads at `SteamGameServer_Init`
2. exports `SteamAppId` into the environment for the same reason
3. overwrites the engine's parsed `steam.inf` value (`g_unSteamAppID`) so the
   advertised appid matches the one the server authenticates with, instead of
   the two disagreeing

Step 3 is a 4-byte write to a global the plugin locates by scanning the engine's
`steam_appid.txt` writer for the instruction that pushes it. The pattern must
match exactly once or the plugin leaves the global alone and says so. The
original value is restored on unload.

Nothing else is touched: no lobby behaviour, no workshop.

## Cross-appid clients (optional)

Because the same build is distributed under two appids, clients launched under
the *other* one present auth tickets the server cannot judge: Steam answers
`k_EBeginAuthSessionResultGameMismatch`, and the engine rejects them. A game
server validates tickets only for the app it logged on as, and that is decided
by Valve's backend, so no amount of local configuration changes it.

Accepting those clients by treating the mismatch as success would be a bad
trade: the SteamID the engine uses comes from the client's own connect packet
and is only trustworthy once Steam has validated the ticket against it, so
anyone could claim any SteamID — breaking bans, admin-by-SteamID and stats.

Instead the plugin opens a **second Steam game server session inside the same
process**, logged on as the other appid, and validates the ticket there.
`BeginAuthSession` binds ticket to SteamID exactly as it would for a native
client, so a forged ID comes back invalid and the rejection stands.

`SteamGameServer_Init` infers its appid from `steam_appid.txt` and owns the one
session steam_api tracks — but the interface underneath takes the appid
explicitly:

```cpp
virtual bool InitGameServer( uint32 unIP, uint16 usGamePort, uint16 usQueryPort,
                             uint32 unFlags, AppId_t nGameAppId, const char *pchVersionString ) = 0;
```

so a second session is built straight from `ISteamClient`: its own pipe, its own
local user, its own game server interface. Its callbacks are pumped with
`Steam_BGetCallback` **on that pipe alone**, so the engine's own dispatch is
untouched. Nothing outlives the server: if it dies, the validator dies with it.

There is nothing to configure. Both appids are fixed constants, the validator
validates whichever one the server is not pinned to, and it is on by default;
`-nocrossappid` turns it off.

Only the mismatch case is diverted, and only on an affirmative pass. Invalid,
expired, duplicate and version mismatch tickets keep the engine's own answer.

**It does not block the server.** `BeginAuthSession` settles `InvalidTicket`,
`GameMismatch` and `ExpiredTicket` synchronously — that is ticket parsing and
the ticket-to-SteamID binding, which is the entire identity question, and it is
answered before the call returns. The `ValidateAuthTicketResponse_t` that
follows carries ban and licence status, not identity, so there is nothing worth
waiting for on the connect path (which runs on the main server thread).

The auth session is left open so that follow-up verdict can still arrive; it is
retired when it does, or after a minute if it never does.

### The follow-up verdict

When it arrives, it is not acted on here. It is handed to the engine's own
`CSteam3Server::OnValidateAuthTicketResponse`, the same function the engine's
Steam session calls, so a cross-appid client is dealt with by exactly the code
that deals with a native one: the ban check, the duplicate-SteamID check, the
`STEAM USERID validated` line, and `NetworkIDValidated` to both the plugin chain
and the game DLL. A VAC ban or missing licence therefore produces the engine's
own rejection, and an OK verdict makes the client count as fully authenticated
instead of leaving it in limbo.

The handler and the `CSteam3Server` singleton are located by signature. The
singleton candidate is only accepted if its `m_eServerMode` holds a valid
`EServerMode`; if either lookup fails the plugin says so and falls back to
logging the verdict, leaving everything else working.

Known limitations, worth understanding before relying on it:

- Anonymous game-server logon has to be permitted for the validated appid.
- Two game server sessions in one process is not a configuration Valve
  documents. `InitGameServer` taking an explicit appid is what makes it possible,
  and per-pipe dispatch (`Steam_BGetCallback` on our pipe only) is what keeps
  the two from interfering, but this is the part to watch first if something
  misbehaves.

## Usage

Drop both files in `csgo/addons/`:

```
csgo/addons/csgo-multi-appid.dll   (Windows)
csgo/addons/csgo-multi-appid.so    (Linux)
csgo/addons/csgo-multi-appid.vdf
```

Plugins in `addons/*.vdf` load automatically on a dedicated server. Default
target is **4465480**; override it on the server command line:

```
srcds_linux -game csgo -appid 730 ...
```

Expected output on startup:

```
csgo-multi-appid: appid pinned to 4465480 (steam.inf said 730)
csgo-multi-appid: the Steam logon picks this up when the server activates; restart the server if it is already logged on
```

It only acts on a dedicated server — on a listen server it logs that it is doing
nothing, because writing `steam_appid.txt` into a client's game directory would
change what the client itself launches as.

## Building

The dedicated server is 32-bit on both platforms, so the plugin is too.

```sh
xmake f -p windows -a x86  -m release -y && xmake   # Windows
xmake f -p linux   -a i386 -m release -y && xmake   # Linux (needs g++-multilib)
```

No SDK checkout is required. The single engine interface the plugin implements
(`IServerPluginCallbacks`, version 004) is mirrored in `src/plugin.cpp`; it has
no virtual destructor, so the vtable layout is the same under MSVC and the
Itanium ABI, and only the declaration order matters. `tier0`'s `Msg`/`Warning`
are resolved at runtime rather than linked.

CI builds both platforms on every push and publishes a rolling `latest` release.
