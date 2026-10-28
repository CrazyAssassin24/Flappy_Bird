# Birds vs Plants — Qt 6 Android port starter

This port keeps the existing Qt Widgets/C++ game, renderer, comic resources, sprites, and gameplay source. It does **not** replace the game with HTML/JavaScript.

## Changes in this starter

- Adds an Android package manifest and requests the Android `INTERNET` permission for Qt Network sockets.
- Starts the app in landscape orientation, which better fits the existing side-panel/game-area layout.
- Removes the game area's fixed 500x500 minimum size so it can fit a phone display.
- Adds Android-only on-screen `BRAKE`, `FLAP`, `BLADE`, and `BOOST` controls to the existing game surface. They emit the same signals as the keyboard controls, so the existing game logic remains the authority.
- Leaves desktop keyboard controls and the existing local multiplayer bindings unchanged.

## Build on Windows with Qt Creator

1. Open **Qt Maintenance Tool** and install the Android component for the same Qt 6 version used by the project. This project archive was previously built with Qt 6.11.1, so use the matching Qt 6.11.1 Android kit where available.
2. Install/configure the Android SDK, NDK, and JDK versions recommended for that Qt kit. Prefer the versions bundled/recommended by the Qt installer instead of choosing arbitrary versions.
3. In Qt Creator, open `DrawLine.pro`.
4. Select an Android kit (start with **Android Qt 6.11.1 Clang arm64-v8a** if installed) in the kit selector. Do not build the Android target using the Windows MinGW/MSVC kit.
5. Enable Developer options and USB debugging on an Android phone, connect it by USB, and accept the debugging prompt. Alternatively, use an Android emulator.
6. Build and Run. Qt Creator packages the app and deploys it to the selected device.

If the Android kit is missing, open **Edit > Preferences > Devices > Android** in Qt Creator and configure the SDK, NDK, and JDK paths. The labels may vary slightly by Qt Creator version.

## Multiplayer scope

The existing `LanNetwork` code uses TCP and is intended for players on the same LAN/Wi-Fi. The Android manifest includes `INTERNET`, which is required for socket use. Cross-internet multiplayer is a separate deployment step: a host behind a home router is not automatically reachable from outside the LAN. It will need a reachable server/relay or appropriate network setup.

## Known unverified items

- The Android arm64-v8a target was built and installed on a Samsung Galaxy A15 for visual verification.
- Test the comic MP4 on a real Android device; media codec/source handling can vary by device and Qt Multimedia build.
- Test hosting and joining over Wi-Fi on real devices, including firewall/router behavior.
- The on-screen controls are designed for Player 1 on phones. Same-keyboard multi-player remains a desktop feature; phone multiplayer should use networked players.


## Mobile menu and scrolling

- The separate right-hand menu panel has been removed from the main layout. The existing menu controls are displayed in a scrollable card centered over the game canvas during mode selection and multiplayer selection.
- On Android, the card has a visible vertical scrollbar and supports one-finger scrolling inside the card. Horizontal scrolling is disabled. The card adapts to the available canvas size so the game area is not resized to make room for a sidebar.
- The game canvas remains unwrapped; world movement is controlled by the existing game rendering/scroll logic.
- Settings and multiplayer-setup dialog tabs retain visible vertical scrollbars and one-finger touch scrolling on Android.
- Gameplay continues to use the full app area. A `MENU` button appears in the game area's upper-right corner after game over and returns to mode selection.

Rebuild the project in Qt Creator after extracting this version.
