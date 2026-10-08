# Birds vs Plants

A small Flappy Bird-style desktop game made with C++ and Qt. It includes an
intro comic and video, Easy, Hard, and Troll modes, and keyboard/mouse controls.

## Requirements

- Windows 10 or later (the project has been built with a 64-bit Windows kit).
- **Qt Creator 20.0.0 Community**.
- A compatible Qt desktop kit. The project’s existing build was generated with
  **Qt 6.11.1 MinGW 64-bit**. Install that Qt version and its matching MinGW
  compiler through the Qt Online Installer if available. Qt Creator is the IDE;
  the Qt libraries and compiler are separate components.
- Qt modules:
  - Qt Core
  - Qt GUI
  - Qt Widgets
  - Qt Multimedia
- Git, if you want to upload the project to GitHub.

The project uses C++17 and qmake. Its `.pro` file requests the Qt modules above.
The comic panels and MP4 video are included in `comic.qrc`, so you do not need
to copy them beside the executable or configure their paths separately. Video
playback depends on the multimedia codecs available to Qt on the computer.

## Open, build, and run with Qt Creator

1. Install Qt Creator 20.0.0 Community and a matching Qt 6 desktop kit with
   MinGW 64-bit. In the Qt installer, make sure the Qt Multimedia component and
   the MinGW toolchain for the selected Qt version are installed.
2. Open Qt Creator.
3. Choose **File > Open File or Project...** and open `DrawLine.pro` from this
   folder. This is the project file, even though the game window is titled
   “Birds vs Plants.”
4. When prompted to configure the project, select the **Desktop Qt 6.11.1
   MinGW 64-bit** kit (or another compatible Qt 6 MinGW 64-bit kit with Qt
   Multimedia installed).
5. Let Qt Creator configure the project. Select the **Debug** or **Release**
   build configuration.
6. Click the green **Run** button, or press **Ctrl+R**. To compile without
   launching, use **Build > Build Project** (or **Ctrl+B**).

If the expected kit is not listed, check **Edit > Preferences > Kits** (on some
systems **Tools > Options > Kits**) and confirm Qt Creator detects the Qt
installation and its matching compiler. Do not reuse the checked-in/generated
build output from another computer; configure a fresh build directory in Qt
Creator.

## How to play

1. Focus the game area and press **Space** or click it to start the intro.
2. During the comic, video, and text sequence, press **Space** or click to
   advance/skip.
3. Choose **Easy**, **Hard**, or **Troll Mode** in the side panel.
4. While playing, press **Space** or left-click the game area to flap.
5. In Hard mode, press **Alt** to fire a feather blade when one is available.
   Blades are awarded as your score increases.
6. After a crash, press **Space** to retry the current mode, or click
   **Restart** to return to mode selection.

Your current score and best score are shown in the side panel. The best score
is kept for the current application run.

## Project files

- `DrawLine.pro` — qmake project configuration and Qt module dependencies.
- `main.cpp`, `mainwindow.cpp`, `mainwindow.h` — application entry point and
  game logic.
- `mainwindow.ui` — Qt Designer window layout.
- `my_label.cpp`, `my_label.h` — game-area input handling.
- `comic.qrc` — embeds the comic images and video into the application.
- `assets/` — comic images and video referenced by `comic.qrc`.

