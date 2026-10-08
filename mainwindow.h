#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QUrl>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <vector>
#include <utility>

namespace Ui {
class MainWindow;
}

// Each pipe is one vertical obstacle with a gap in the middle.
// In hard mode, a "plant eater" may emerge from the pipe to attack the bird.
struct Pipe {
    float x;
    int gapTop;
    int gapHeight;
    bool scored;
    bool hasEater;
    bool fakePipe;
    bool fakeActivated;
    bool gapShifted;
    int originalGapTop;
    bool windAffected;
    bool eaterFromTop;
    bool eaterTriggered;
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
    void onFlap();
    void onShoot();
    void updateGame();
    void onEasyClicked();
    void onHardClicked();
    void onTrollClicked();
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

    float birdY;    // bird vertical position in grid cells
    float birdVel;   // bird current vertical velocity
    int birdX;      // bird horizontal position in grid cells
    static constexpr int BIRD_W = 4;
    static constexpr int BIRD_H = 4;
    static constexpr int CANNON_W = 8;
    static constexpr int CANNON_H = 8;
    static constexpr float CACTUS_BALL_RADIUS = 2.0f;

    // Feather blade tuning.
    static constexpr int   FEATHER_BLADE_GAP   = 20;   // height (cells) of the hole a blade cuts in a log
    static constexpr float FEATHER_BLADE_SPEED = 3.0f; // cells per tick, flying right
    static constexpr int   FEATHER_BLADE_LEN   = 3;    // drawn length in cells
    static constexpr int   BLADE_SCORE_INTERVAL = 10;  // score needed between blades
    static constexpr int   CANNON_HITS_TO_KILL  = 1;  // blade hits to destroy the hard-mode cannon
    int birdScale;
    int birdScaleMs;

    static constexpr float GRAVITY    = 0.35f; // downward acceleration each tick
    static constexpr float FLAP_VEL   = -3.2f; // upward impulse when player jumps
    static constexpr float MAX_FALL   = 6.0f;  // terminal falling speed

    std::vector<Pipe> pipes;
    float pipeSpeed;
    int pipeWidth;
    int pipeGap;
    int pipeSpacing;
    float distSinceLastPipe;

    static constexpr int easyPipeGap = 40;
    static constexpr float easyPipeSpeed = 1.2f;
    static constexpr int easyPipeWidth = 10;
    static constexpr int easyPipeSpacing = 55;

    static constexpr int hardPipeGap = 40;
    static constexpr float hardPipeSpeed = 1.2f;
    static constexpr int hardPipeWidth = 10;
    static constexpr int hardPipeSpacing = 55;
    static constexpr float hardPipeSpeedFast = 1.7f;
    static constexpr float hardSpeedBump = 0.15f;

    int groundH;       // number of rows used for the ground strip
    float groundOffset; // scrolling offset for the ground and background motion

    int score;
    int highScore;
    bool hardMode;     // true = harder gameplay and extra enemy behavior
    bool trollMode;    // true = chaotic troll mechanics

    float bobTimer;    // animation timer used for title screen bobbing
    QTimer *gameTimer; // main game tick timer; controls update frequency

    int comicPanel;
    int comicMs;
    QMediaPlayer *comicVideoPlayer;
    QVideoSink *comicVideoSink;
    QVideoFrame comicVideoFrame;
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

    bool hasBlade = false;                 // a blade is stocked and ready to fire
    int nextBladeScore = BLADE_SCORE_INTERVAL; // score at which the next blade is granted
    std::vector<FeatherBlade> blades;      // 0 or 1 blades in flight

    static constexpr int LETTER_W = 3;
    static constexpr int LETTER_H = 4;

    int hardPlayMs;      // elapsed time in hard mode, used for flash events
    int flashMsLeft;     // remaining time for the white flash effect
    int trollPauseMs;    // fake GAME OVER pause before gameplay resumes
    int trollEventMs;    // timer used to trigger random troll events
    float windX;         // temporary horizontal wind force
    int windAnimMs;      // animation phase while the wind is active
    int fakeGapMs;       // duration of a temporary fake-gap shift
    static constexpr int FLASH_EVERY_MS = 150000; // every ~150s in hard mode, flash event triggers
    static constexpr int FLASH_DURATION_MS = 1000; // white flash lasts 1 second
    static constexpr int TICK_MS = 33; // game loop interval; ~30 FPS

    void renderFrame();
    void fillCell(QImage &img, int cx, int cy, int r, int g, int b);
    void drawBird(QImage &img);
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
    void drawBladeHud(QImage &img);
    void drawWind(QImage &img);
    void drawFlash(QImage &img);
    void drawTrollOverlay(QImage &img);
    void drawText(QImage &img, const QString &text, int centerX, int centerY,
                  int r, int g, int b, int fontSize = 14);

    void spawnPipe();
    bool checkCollision();
    void resetGame();
    void updateGridSize();
    void applyDifficulty();
    void updateHardSpeed();
    void showModeSelect();
    void beginPlay(bool hard, bool troll = false);
    void skipOrAdvanceComic();
    void startComicVideo();
    void finishComicVideo();
    void onComicVideoFrameChanged(const QVideoFrame &frame);

    void updateHeadingLetters();
    void tryDropHeadingLetter();
    void updateEaters();
    void updateHardCannon();
    void updateBlades();
    void tryAwardBlade();
    void cutPipeAt(Pipe &p, float y);
    bool pipeRowSolid(const Pipe &p, int row) const;
    void updateTroll();
    bool letterHitsBird(const FallingLetter &L) const;
    bool eaterHitsBird(const Pipe &p) const;
    int currentBirdW() const;
    int currentBirdH() const;
    int headingSlotPixelX(int slot, int fontSize) const;
};

#endif // MAINWINDOW_H
