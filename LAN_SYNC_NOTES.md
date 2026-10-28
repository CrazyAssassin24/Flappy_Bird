# LAN sync changes (matching the Windows build)

These keep the Android and Windows builds speaking the same LAN protocol, so either
device can host and the other can join. The wire format itself was already compatible:
a Windows host sends exactly the messages (`lan_init` + compact `state`) this build reads.

## Changes
- **"GET READY" hold.** Every hosted LAN round starts with a 3-second hold in which nothing
  moves (see `LanCountdownMs` in `mainwindow.h`; set it to 0 to disable). Previously gravity
  acted from the first tick, so whichever player was not yet touching their controls when the
  last player connected lost their bird within about a second, and a dead bird is not drawn
  until the final screen. The countdown value is sent in the state snapshot as `countdownMs`
  and drawn on every screen.
- **Title-letter refill animation (Troll mode).** The joinee's renderer reads
  `headingRefillSlot/T/Wait`, but the host did not send them, so that animation never
  played on joinees. The host now sends them.
- **World-size changes mid-match.** The snapshot no longer carries the world size, so when the
  host's grid changes (for example the phone is rotated) the host re-sends `lan_init`.
- **Game-over sound on joinees.** Joinees now play the crash sound when they see the round end.
- **Scaling a large host world.** `my_label` samples smoothly when it shrinks a big host
  picture (e.g. a PC window) onto the phone screen instead of dropping pixels.

## Not changed
Everything else, including `TICK_MS = 50` for phones. Note that game physics advance per
tick, so the *host's* tick rate sets the game speed: a match hosted on the phone runs slower
than one hosted on the PC (33 ms).
