# Windows port of the Android build's additions

This documents what was brought over from the Android build (`ndroid`) into this
desktop build, what was deliberately left out, and where this build differs from
the Android code on purpose. Nothing here has been compiled by the person who
made the changes (no Qt toolchain was available): build it in Qt Creator and test
on two real PCs before relying on it.

## Brought over

**Music and sound** (`mainwindow.*`, `gamesettings.*`, `comic.qrc`, `assets/`)
- Looping background music, a cinematic bed under the opening comic/video/title
  card, and short accents for panel changes, video reveal, title reveal and game over.
- *Audio* tab in Settings (music on/off, volume). Presets and a LAN host never
  change a player's own audio settings.
- If the comic video cannot be decoded it is skipped instead of hanging the intro.
- Resources are copied to the cache folder by `extractResourceToCache()` (see below).

**Multiplayer / joinee improvements** (`lan_network.*`, `mainwindow.*`, `my_label.*`)
- `TCP_NODELAY` on every socket (avoids Nagle/delayed-ACK stalls), targeted
  `sendMessageToPeer`, and stale-snapshot dropping when a peer's write queue backs up.
- `lan_init` message: match settings + world size sent once per round / per peer,
  so the 30 Hz snapshots no longer carry the full configuration.
- A joinee keeps the host's grid size instead of recomputing it from its own window
  (`updateGridSize`), and goes back to its own grid when it leaves the match.
- The joinee no longer renders twice per packet (once on receipt, once on its timer).
- `my_label::paintEvent` scales and letterboxes the host's picture to the local
  window with the aspect ratio kept; the picture can no longer dictate window size.
- Joinee-side interpolation of bird and pipe positions between snapshots.
- Comic panels get a dimmed full-bleed backdrop instead of black bars (composed
  once per panel/size and cached).

## Deliberately NOT brought over
- Everything Android-only: `android/`, manifest/packaging in the `.pro`, the touch
  control buttons, the phone-style overlay menu card and scroll area, and the
  `Q_OS_ANDROID` dialog sizing code.
- `ui_mainwindow.h`: a uic-generated file that had been left in the Android zip.
- `TICK_MS = 50`. Game physics advance per tick, so a 50 ms tick runs the game at
  about two thirds speed. This build keeps 33 ms (about 30 FPS), as `gamesettings.h` expects.
- `QVideoWidget` for the comic video. This build keeps the existing `QVideoSink`
  path, which works on Windows, and so does not need the `multimediawidgets` module.

## Differences from the Android code (on purpose)
1. **Read-only cache files.** Files copied out of Qt resources are read-only, and
   Windows cannot delete a read-only file, so the Android approach would have failed
   to refresh the intro track on every launch after the first. `extractResourceToCache()`
   clears the flag first, and re-uses an identical existing file if a second running
   copy of the game has it locked (e.g. host and joinee on one PC for testing).
2. **Title-letter refill animation.** The Android snapshot dropped
   `headingRefillSlot/T/Wait`, which the renderer reads in Troll mode. They are kept.
3. **Snapshot rate limit.** The Android 33 ms limit could skip alternate snapshots
   when a 33 ms timer ticks slightly early (normal on Windows). The limit is 25 ms.
4. **Host window resized mid-match.** Because the world size is no longer in every
   snapshot, the host re-sends `lan_init` when its grid size changes.
5. **Game-over sound on joinees.** The joinee now plays it when it sees the round end.
6. **Frame size policy.** The frame keeps its `.ui` size policy and stretch; the
   size-hint overrides in `my_label` are what stop the picture resizing the window.

## Known limitation
The interpolation is ported as designed, but the joinee's render timer and the
host's snapshots both run at ~30 Hz, so it effectively shows the newest snapshot.
Real smoothing would need a faster joinee render loop, at the cost of about one
snapshot (~33 ms) of extra visual latency.

## Update: round-start hold, and picture quality

- **"GET READY" hold for LAN rounds.** Gravity used to start acting on the very first
  tick, and a bird that is out disappears until the final screen. When the PC hosted,
  the round began the instant the phone connected, so a host not yet at the keyboard
  lost their bird within about a second and the joinee saw no opponent. Every LAN round
  now begins with a 3-second hold (all birds still, countdown drawn on every screen, flaps
  ignored). The countdown value travels in the state snapshot as `countdownMs`. Change or
  disable it with `LanCountdownMs` in `mainwindow.h`.
- **Desktop picture path.** `my_label::paintEvent` now hands the picture to the stock
  `QLabel` painter whenever it is within a few pixels of the widget size, which is always
  the case for the host and for single player. It therefore draws exactly as it did before
  this port. Only a picture that genuinely needs resizing (a joinee showing a differently
  sized host world) takes the custom path, and when that path has to shrink the picture
  it now uses area-averaged resampling at device resolution instead of plain bilinear.
- **Comic panel backdrop** is now a real blur of the panel rather than a dimmed, enlarged
  copy, which could read as a ghost/double image.
