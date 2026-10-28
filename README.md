# Birds vs Plants — LAN multiplayer build

Qt Widgets / C++17 game. Open `DrawLine.pro` in Qt Creator (Qt 6 with Widgets, Multimedia, and Network modules) and build.

## LAN multiplayer

- Host a room from **Multiplayer → Host LAN**. The settings dialog lets the host choose 2–4 total players, Co-op or Versus, and Easy/Hard/Troll rules.
- Other players choose **Multiplayer → Join LAN** and enter the host computer's IPv4 address. All machines must be on the same LAN/Wi-Fi and the host firewall must allow the app on TCP port `45454`.
- The host starts the match when the configured number of players has joined. Host is Player 1; remote clients are assigned Player 2–4.
- The host is authoritative: physics, collisions, pipes, scoring, lives, blade attacks, Hard-mode hazards, and Troll-mode events run on the host. Clients send inputs and render host state snapshots.
- TCP packets use newline-delimited compact JSON. TCP message framing is handled explicitly because TCP does not preserve write boundaries.
- Joinee-friendly protocol: the host sends a `lan_init` message (match settings and world size) at the start of every round and to every peer that connects, then streams compact state snapshots at no more than ~30 Hz. Sockets use `TCP_NODELAY`, and stale snapshots are dropped rather than queued if a peer's connection falls behind.
- A joinee always draws the host's world, whatever its own window size: the finished picture is scaled to fit the window (letterboxed, aspect ratio kept) instead of being cropped or forcing the window to resize.
- Every LAN round (including restarts) starts with a 3-second "GET READY" hold in which nothing moves, so a host who is not yet at the controls when the last player connects is not already out. Flaps are ignored during the hold. Set `LanCountdownMs` in `mainwindow.h` to 0 to disable it.
- All players must run the same build. A joinee running an older build cannot read a newer host's compact snapshots.

## Music and sound

- A looping background track plays during the game; a separate cinematic track plays under the opening comic, video and title card. Both are bundled in `assets/` (see `comic.qrc`) and copied to the per-user cache folder at startup because `QMediaPlayer` streams from real files.
- Short sound accents play on comic panel changes, the video reveal, the title reveal, and when a round ends (game over).
- **Settings → Audio** has a *Background music* switch and a *Music volume* slider. These are local preferences: a LAN host's settings never overwrite a joinee's audio settings, and difficulty presets never change them.

## Controls

Every machine uses its own Player 1 control keys for its assigned bird:

- `Space` — flap / restart after game over
- `Left Arrow` — brake
- `Right Arrow` — thrust
- `Alt` — fire a feather blade (when enabled)

Local same-keyboard multiplayer remains available. In local multiplayer, P1–P4 continue to use their existing key maps.

## Notes / current limitations

- LAN supports up to four total players and synchronizes Co-op/Versus, Easy/Hard/Troll mechanics, blades, pipes, cannon hazards, Troll effects, scoring, and game-over state through authoritative snapshots.
- This is a LAN-only build; it does not provide internet matchmaking, NAT traversal, encryption, or anti-cheat protection.
- A disconnected remote player's bird is eliminated. A host disconnect ends the client's session.
- The networking changes need to be compiled and tested on two or more real computers in Qt Creator. The editing environment did not have a Qt build toolchain, so no successful local Qt compilation is claimed.
