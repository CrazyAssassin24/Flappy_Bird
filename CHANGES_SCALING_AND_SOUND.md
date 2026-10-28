# Changes: LAN cross-device scaling + background music

## Scaling (Hardware Accelerated)
- Removed CPU software rescaling and per-frame `QImage canvas` heap allocation in `MainWindow::renderFrame()`.
- Implemented hardware-accelerated letterboxing and aspect-ratio scaling directly in `my_label::paintEvent()`.
- `my_label` overrides `sizeHint()` and `minimumSizeHint()` so large host frames don't distort client window layouts.
- LAN client preserves host grid coordinates so physics/geometry remain identical across devices.

## Audio
- All sound effects (flap, score, hit, blade, die) and the procedural WAV synthesiser were removed completely.
- Added looping background music: `assets/background_music.ogg` (calm, original ~40 s loop), registered in `comic.qrc` under `/music`.
- Played with `QMediaPlayer` + `QAudioOutput` (`MainWindow::initMusic()` / `applyMusicSettings()`); loops forever.
- Normal background music stays paused throughout the opening sequence and starts when the mode-selection menu is ready.
- Settings dialog "Audio" tab now has "Background music" (on/off) and "Music volume". Local preference, never overwritten by a LAN host.

- The intro cinematic bed (`assets/intro_cinematic.ogg`) is a 32-second original, rhythmic cinematic/action cue with a restrained opening, pulsing bass, melodic plucks, and a stronger chase section. It begins at application startup and continues across the comic panels, video and title card. When the mode-selection menu appears, the intro track stops and the existing looping background music takes over.
- Added three low-latency one-shot transition accents (`panel_transition.wav`, `video_reveal.wav`, and `title_reveal.wav`) via `QSoundEffect`. These are triggered from the actual panel/video/title state transitions, so cues follow automatic timing and manual Space advances rather than relying only on fixed track timestamps.
- Transition accents obey the same music on/off and master volume settings, and are intentionally mixed below the main intro bed.
