# Birds vs Plants — LAN multiplayer build

Qt Widgets / C++17 game. Open `DrawLine.pro` in Qt Creator (Qt 6 with Widgets, Multimedia, and Network modules; background music is `assets/background_music.ogg`) and build.

## LAN multiplayer

- Host a room from **Multiplayer → Host LAN**. The settings dialog lets the host choose 2–4 total players, Co-op or Versus, and Easy/Hard/Troll rules.
- Other players choose **Multiplayer → Join LAN** and enter the host computer's IPv4 address. All machines must be on the same LAN/Wi-Fi and the host firewall must allow the app on TCP port `45454`.
- The host starts the match when the configured number of players has joined. Host is Player 1; remote clients are assigned Player 2–4.
- The host is authoritative: physics, collisions, pipes, scoring, lives, blade attacks, Hard-mode hazards, and Troll-mode events run on the host. Clients send inputs and render host state snapshots.
- TCP packets use newline-delimited compact JSON. TCP message framing is handled explicitly because TCP does not preserve write boundaries.

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
