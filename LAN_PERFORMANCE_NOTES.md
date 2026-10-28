# LAN multiplayer performance changes

This revision adds a second optimization pass focused on the host-to-joinee path:

- Static mode/configuration and world dimensions are sent in a targeted `lan_init` message when a peer joins and broadcast again once when a new round starts, rather than being repeated in every 30 Hz state snapshot.
- The host handles late joiners by sending the current initialization data directly to that peer.
- Periodic state snapshots no longer repeat mode/configuration, world dimensions, or host-only simulation counters that the joinee does not need to render.
- State snapshots are capped at approximately 30 Hz.
- Stale state snapshots are skipped when a peer's TCP write queue exceeds 32 KiB. Control, input and initialization messages are not dropped by this rule.
- TCP low-latency mode is enabled on client and accepted sockets.
- The joinee applies each snapshot without directly triggering a second render.
- The joinee renders at its local timer rate and interpolates bird and stable-index pipe positions between the previous and latest authoritative snapshots. Pipe interpolation is skipped across snapshots where the pipe count changes. This is visual-only: the host remains authoritative for physics, collisions, scores and ability outcomes.
- Joinee touch controls are explicitly shown while the received state is `Playing`, restoring the local Android Brake, Blade and Boost controls.

## Remaining protocol limitation

This is a hybrid initialization + compact periodic-snapshot protocol, not a complete per-object delta/event protocol. Bird, pipe, projectile and falling-letter arrays are still included in snapshots, so the largest remaining network cost is repeated dynamic object data. The next step beyond this pass would assign stable IDs to objects and send spawn/change/remove events, while retaining occasional full snapshots for recovery. This needs testing against every game mode to avoid visual or gameplay desynchronization.

## Validation

The ZIP archive can be integrity-checked, but this environment does not have the Qt build toolchain installed, so an Android APK build and two-device LAN test have not been performed. Please build in the same Qt version/toolchain used for the existing project and test host + joinee on the same Wi-Fi LAN before relying on the changes.
