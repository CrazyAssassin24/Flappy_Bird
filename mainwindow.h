#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QAudioOutput>
#include <QSoundEffect>
#include <QUrl>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QString>
#include <vector>
#include <utility>
#include "gamesettings.h"
#include "lan_network.h"

namespace Ui {
class MainWindow;
}

// Each pipe is one vertical obstacle with a gap in the middle.
// In hard mode, a "plant eater" may emerge from the pipe to attack the bird.
struct Pipe {
    float x;
    int gapTop;
    int gapHeight;
    bool scored;          // passed by at least one bird (co-op score / speed ramp)
    unsigned scoredMask;  // bit i set = bird i has been awarded this pipe
    bool hasEater;
    bool fakePipe;
    bool fakeActivated;
    bool gapShifted;
    int originalGapTop;
    bool windAffected;
    bool eaterFromTop;
    bool eaterTriggered;
    int  seed = 0;        // per-pipe random seed so each trunk's bark/knots look different
    float eaterOut;
    float eaterMax;
    // Holes cut by feather blades: [top, bottom) row ranges, in grid cells.
    std::vector<std::pair<int,int>> extraGaps;
};

// A feather blade in flight. x is the leading tip, y is the row it travels along.
struct FeatherBlade {
    float x;
    float y;
};

// One player's bird. Single player uses birds[0]; multiplayer has 2-4.
struct Bird {
    int   id = 0;
    float x = 0;            // left edge, grid cells
    float y = 0;            // top edge, grid cells
    float vel = 0;          // vertical velocity
    int   scale = 1;        // troll-mode size multiplier
    int   scaleMs = 0;
    float energy = 100.0f;
    float speedMul = 1.0f;
    bool  thrustKey = false, brakeKey = false;        // keys currently held
    bool  thrustActive = false, brakeActive = false;  // actually in effect this tick
    float wingPhase = 0.0f;
    float tilt = 0.0f;
    int   flapAnimMs = 0, shootAnimMs = 0;
    bool  hasBlade = false;
    int   nextBladeScore = 0;
    std::vector<FeatherBlade> blades;
    int   score = 0;
    int   lives = 2;
    bool  alive = true;
    int   invulnMs = 0;     // respawn protection
    int   diedAtMs = 0;     // play time when the bird ran out of lives
    int   hue = 0;          // colour shift of the sprite (degrees)
};

// Falling letters are the animated title letters that drop after the heading breaks apart.
struct FallingLetter {
    QChar ch;           // letter character being shown
    int slotIndex;      // index in the original heading text
    float x;            // horizontal position in grid cells
    float y;            // vertical position in grid cells
    float velY;         // falling speed for gravity animation
    bool grounded;      // true once the letter lands on the ground
};

struct CactusBall {
    float x;
    float y;
    float vx;
    float vy;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onFlap(int player);
    void onShoot(int player);
    void onThrustHeld(int player, bool held);
    void onBrakeHeld(int player, bool held);
    void updateGame();
    void onEasyClicked();
    void onHardClicked();
    void onTrollClicked();
    void onMultiplayerClicked();
    void onLocalMultiplayerClicked();
    void onHostLanClicked();
    void onJoinLanClicked();
    void onMultiplayerBackClicked();
    void onSettingsClicked();
    void onRestartClicked();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    Ui::MainWindow *ui;

    // The game moves through a small state machine.
    // IntroWait: the player starts the comic with Space.
    // ComicIntro: five comic panels are shown one at a time.
    // Typewriter: the final question appears before mode selection.
    // ModeSelect: user chooses easy or hard difficulty.
    // Playing: the actual Flappy Bird gameplay loop runs.
    // GameOver: bird has crashed and restart flow is available.
    enum GameState { IntroWait, ComicIntro, ComicBlackBeforeVideo, ComicVideo, ComicWhiteFade, Typewriter, ModeSelect, Playing, GameOver };
    GameState state;

    int gridW, gridH;
    static constexpr int CELL = 5;

    // ---- All tunable values live in cfg (see gamesettings.h) ----
    GameConfig cfg;

    // ---- Birds ----
    std::vector<Bird> birds;   // 1 in single player, cfg.mpPlayerCount in multiplayer
    int homeX = 15;            // start column of the birds (grid cells)
    bool multiplayer = false;
    int winner = -1;           // multiplayer result: bird index, -1 = none / draw
    int gameOverMs = 0;        // time since game over (restart lock)
    int playMs = 0;            // time spent playing this round
    int pipesSpawned = 0;      // for the shrinking gap
    int coopScore = 0;         // shared score in co-op

    static constexpr int CANNON_W = 8;
    static constexpr int CANNON_H = 8;
    static constexpr float CACTUS_BALL_RADIUS = 2.0f;

    // ---- Bird animation timing ----
    static constexpr int FLAP_ANIM_MS  = 300;   // one wing beat after a flap
    static constexpr int SHOOT_ANIM_MS = 300;   // wing-throw pose when firing a feather blade

    // Helpers that read the per-bird state through cfg.
    bool canAct(const Bird &b) const { return cfg.unlimitedEnergy || b.energy >= cfg.energyRedMax; }
    int  birdW(const Bird &b) const { return cfg.birdSize * b.scale; }
    int  birdH(const Bird &b) const { return cfg.birdSize * b.scale; }
    int  birdCol(const Bird &b) const { return qRound(b.x); }
    float worldSpeed() const;   // current scroll speed in cells per tick
    float refSpeedMul() const;  // speed multiplier of the reference bird (Player 1)
    void updateBirdAnim(Bird &b);
    void updateEnergy(Bird &b);
    void respawnBird(Bird &b);
    void killBird(Bird &b);
    void resolveBirdBumps();
    void checkMatchEnd();
    void startGameOver();
    void refreshScore();

    QImage vigCache;  // cached vignette overlay
    double bgScroll = 0.0;  // continuous (never wrapped) scroll distance in cells, for seamless parallax
    QImage bgCache;   // pre-rendered sky/haze/vignette, rebuilt when the size changes

    std::vector<Pipe> pipes;
    float pipeSpeed;
    int pipeWidth;
    int pipeGap;        // runtime copy of cfg (may be changed by difficulty)
    int pipeSpacing;
    float distSinceLastPipe;

    int groundH;       // number of rows used for the ground strip
    float groundOffset; // scrolling offset for the ground and background motion

    int score;         // single player: the bird's score; multiplayer: leader / team score
    int highScore;
    bool hardMode;     // true = harder gameplay and extra enemy behavior
    bool trollMode;    // true = chaotic troll mechanics

    float bobTimer;    // animation timer used for title screen bobbing
    QTimer *gameTimer; // main game tick timer; controls update frequency
    LanNetwork *lanNetwork = nullptr;
    bool lanClientMode = false;
    bool lanMatchActive = false;
    int lanLocalPlayerId = 0;
    bool dispatchingLanInput = false;
    int lanCountdownMs = 0;   // > 0 while a LAN round is in its "get ready" hold (host-driven)

    // ---- Background music (looping track bundled in resources) ----
    QMediaPlayer *musicPlayer = nullptr;
    QAudioOutput *musicOutput = nullptr;
    void initMusic();
    void applyMusicSettings();
    void playComicTransitionCue(QSoundEffect *effect, float gain);
    void playGameOverCrash();

    int comicPanel;
    int comicMs;
    QMediaPlayer *comicVideoPlayer;
    QVideoSink *comicVideoSink;
    QVideoFrame comicVideoFrame;
    QMediaPlayer *introAudioPlayer = nullptr;
    QAudioOutput *introAudioOutput = nullptr;
    QSoundEffect *panelTransitionSound = nullptr;
    QSoundEffect *videoRevealSound = nullptr;
    QSoundEffect *titleRevealSound = nullptr;
    QSoundEffect *gameOverCrashSound = nullptr;
    bool comicVideoReady = true;     // false if the cinematic failed to load/play (it is then skipped)
    QImage comicIntroCache;          // pre-composed comic panel (backdrop + panel) for the current size
    int comicIntroCachePanel = -1;
    int comicTransitionMs;
    bool comicVideoStarted;
    int typewriterCharsShown;
    int typewriterTick;
    int typewriterWait;
    std::vector<QPixmap> comicPanels;
    static constexpr int COMIC_PANEL_MS = 4000;
    static constexpr int COMIC_BLACK_MS = 500;
    static constexpr int COMIC_WHITE_MS = 500;
    static constexpr int COMIC_WHITE_FADE_MS = 900;
    static constexpr int TYPEWRITER_TICKS_PER_CHAR = 2;

    std::vector<FallingLetter> fallingLetters; // letters falling from the heading in troll mode
    QString headingText;                      // title text: "FLAPPY BIRD -V.3.0.1"
    std::vector<char> headingInPlace;          // 1=letter still displayed in title, 0=letter dropped
    int headingDropDelay;                     // delay before dropping another title letter
    int headingRefillSlot;                    // index of letter currently being refilled
    float headingRefillT;                      // refill animation progress 0..1
    int headingRefillWait;                    // short wait before letter returns to title

    std::vector<CactusBall> cactusBalls;
    float cannonX;
    float cannonY;
    float cannonVelX;
    float cannonVelY;
    float cannonAimX;
    float cannonAimY;
    int cannonFireMs;
    int cannonDirectionMs;
    int cannonHits = 0;
    bool cannonAlive = true;

    static constexpr int LETTER_W = 3;
    static constexpr int LETTER_H = 4;

    int hardPlayMs;      // elapsed time in hard mode, used for flash events
    int flashMsLeft;     // remaining time for the white flash effect
    int trollPauseMs;    // fake GAME OVER pause before gameplay resumes
    int trollEventMs;    // timer used to trigger random troll events
    float windX;         // temporary horizontal wind force
    int windAnimMs;      // animation phase while the wind is active
    int fakeGapMs;       // duration of a temporary fake-gap shift
    static constexpr int TICK_MS = 33; // game loop interval; ~30 FPS

    // A LAN round starts with a short "get ready" hold: nothing moves, so a host whose
    // player is not yet at the controls is not out before the round has really begun.
    static constexpr int LanCountdownMs = 3000;

    void renderFrame();
    void fillCell(QImage &img, int cx, int cy, int r, int g, int b);
    void drawBird(QImage &img, const Bird &b);
    void drawBirdNames(QImage &img);
    void drawPlayerHud(QImage &img);
    void drawMultiplayerOver(QImage &img);
    void drawPipes(QImage &img);
    void drawGround(QImage &img);
    void drawForestBg(QImage &img);
    void drawTrees(QImage &img);
    void drawStartScreen(QImage &img);
    void drawIntroPromptScreen(QImage &img);
    void drawComicIntroScreen(QImage &img);
    void drawComicBlackScreen(QImage &img);
    void drawComicVideoScreen(QImage &img);
    void drawComicWhiteFadeScreen(QImage &img);
    void drawTypewriterScreen(QImage &img);
    void drawModeSelectScreen(QImage &img);
    void drawGameOverScreen(QImage &img);
    void drawHeading(QImage &img);
    void drawFallingLetters(QImage &img);
    void drawEaters(QImage &img);
    void drawCannon(QImage &img);
    void drawCactusBalls(QImage &img);
    void drawBlades(QImage &img);
    void drawBladeHud(QImage &img, const Bird &b);
    void drawEnergyBar(QImage &img, const Bird &b, int centerX, int barW, int y0);
    void updateEnergy();
    void drawWind(QImage &img);
    void drawFlash(QImage &img);
    void drawTrollOverlay(QImage &img);
    void drawText(QImage &img, const QString &text, int centerX, int centerY,
                  int r, int g, int b, int fontSize = 14);

    void spawnPipe();
    bool checkCollision(const Bird &b);
    void resetGame();
    void updateGridSize();
    void applyDifficulty();
    void updateHardSpeed();
    void showMultiplayerOptions();
    void showModeSelect();
    void beginPlay(bool hard, bool troll = false, bool multi = false);
    void skipOrAdvanceComic();
    void startComicVideo();
    void finishComicVideo();
    void onComicVideoFrameChanged(const QVideoFrame &frame);
    void handleLanMessage(const QJsonObject &message);
    void sendLanInput(const QString &action, bool held = false);
    void broadcastLanState();
    void applyLanState(const QJsonObject &message);
    void sendLanInit(int peerId = -1);   // static match settings + world size; peerId < 1 = everyone

    // Client-side visual interpolation: the host remains authoritative; this only
    // smooths bird / pipe rendering between the previous and latest snapshots.
    std::vector<std::pair<float, float>> lanPreviousBirdPositions;
    std::vector<std::pair<float, float>> lanTargetBirdPositions;
    std::vector<float> lanPreviousPipeX;
    std::vector<float> lanTargetPipeX;
    int lanInterpolationElapsedMs = 33;

    void updateHeadingLetters();
    void tryDropHeadingLetter();
    void updateEaters();
    void updateHardCannon();
    void updateBlades(Bird &b);
    void tryAwardBlade(Bird &b);
    void cutPipeAt(Pipe &p, float y);
    bool pipeRowSolid(const Pipe &p, int row) const;
    void updateTroll();
    bool letterHitsBird(const FallingLetter &L, const Bird &b) const;
    bool eaterHitsBird(const Pipe &p, const Bird &b) const;
    int headingSlotPixelX(int slot, int fontSize) const;
};

#endif // MAINWINDOW_H
