#ifndef GAMESETTINGS_H
#define GAMESETTINGS_H

#include <QDialog>
#include <QStringList>
#include <vector>

class QWidget;
class QComboBox;

// ---------------------------------------------------------------------------
// GameConfig: every tunable value of the game, each with its OWN variable name.
//
// The defaults reproduce the original single-player game exactly, so Easy /
// Hard / Troll behave as before until the player changes something in the
// settings dialog.  Distances are in grid cells (1 cell = 5 pixels), times in
// milliseconds, per-tick values are per 33 ms game tick.
//
// To add a new setting: add a member here, add one line to configFields() in
// gamesettings.cpp (that makes it appear in the dialog and be saved), then use
// `cfg.yourMember` in the game code.
// ---------------------------------------------------------------------------
struct GameConfig
{
    // ===== Audio =====
    bool  musicEnabled         = true;   // play background music
    float musicVolume          = 0.4f;   // 0..1

    // ===== Multiplayer / match =====
    int   mpPlayerCount        = 2;      // 2..4 birds on one keyboard
    int   mpMatchType          = 0;      // 0 = Versus (last bird standing), 1 = Co-op (shared score)
    bool  mpHardMode           = false;  // enable all hard-mode features in multiplayer
    bool  mpTrollMode          = false;  // enable all troll-mode features in multiplayer
    int   mpTargetScore        = 0;      // Versus: first to this score wins (0 = off)
    int   mpBirdCollision      = 0;      // 0 = birds pass through each other, 1 = birds bump
    float mpHorizontalSpeed    = 1.0f;   // how far thrust/brake pushes a bird across the screen
    float mpReturnSpring       = 0.02f;  // pull back to the start column when not thrusting
    int   mpGameOverLockMs     = 800;    // delay before a key press can restart after game over

    // ===== Lives (all modes) =====
    int   livesPerPlayer       = 1;      // crashes allowed before a bird is out
    int   respawnInvulnMs      = 1500;   // invulnerable time after losing a life

    // ===== Pipes =====
    int   pipeGap              = 50;     // height of the opening (cells)
    int   pipeWidth            = 12;     // thickness of a pipe (cells)
    int   pipeSpacing          = 65;     // distance between pipes (cells)
    float pipeSpeed            = 1.2f;   // world scroll speed = "bird speed" (cells / tick)
    int   pipeGapMargin        = 3;      // min distance of the gap from ceiling / ground
    float pipeGapShrinkPerPipe = 0.0f;   // gap shrinks by this much per pipe spawned
    int   pipeGapMin           = 25;     // the gap never shrinks below this

    // ===== Bird physics =====
    float gravity              = 0.30f;  // downward acceleration per tick
    float flapStrength         = 3.2f;   // upward impulse of a flap
    float maxFallSpeed         = 6.0f;   // terminal falling speed
    int   birdSize             = 4;      // hit-box size (cells, square); sprite scales with it

    // ===== Speed control (Right = thrust, Left = brake) =====
    float speedMulMin          = 0.55f;  // slowest allowed speed multiplier (braking limit)
    float speedMulMax          = 1.80f;  // fastest allowed speed multiplier (thrust limit)
    float thrustAccel          = 0.035f; // acceleration per tick while thrusting
    float brakeDecel           = 0.035f; // deceleration per tick while braking
    float idleDrag             = 0.02f;  // pull back to normal speed when idle

    // ===== Energy =====
    bool  unlimitedEnergy      = false;  // flap / thrust / brake cost nothing
    float energyMax            = 100.0f;
    float energyGreenMin       = 60.0f;  // at or above: green
    float energyRedMax         = 25.0f;  // below: red, flapping locked
    float flapCost             = 3.5f;   // per flap
    float thrustCost           = 0.9f;   // per tick while thrusting
    float brakeCost            = 0.9f;   // per tick while braking
    float energyRegen          = 0.25f;  // per tick while not thrusting / braking

    // ===== Feather blades & cannon =====
    bool  bladesEnabled        = true;   // Alt (P1) fires a feather blade
    bool  startWithBlade       = true;   // a blade is ready at the start
    int   bladeGap             = 20;     // height of the hole a blade cuts in a pipe
    float bladeSpeed           = 3.0f;   // cells per tick
    int   bladeLength          = 3;      // drawn length (cells)
    int   bladeScoreInterval   = 2;     // score needed between blades
    int   cannonHitsToKill     = 1;      // blade hits to destroy the cannon

    // ===== Hard mode features =====
    float hardSpeedBonus       = 0.3f;   // added to pipeSpeed in hard mode
    bool  hardPlantEaters      = true;   // plants lunge out of pipes
    float eaterTriggerDistance = 16.0f;  // how close the bird must be (cells)
    float eaterMaxExtension    = 5.0f;   // how far a plant reaches into the gap (cells)
    float eaterGrowSpeed       = 0.45f;  // cells per tick
    float eaterRetractSpeed    = 0.35f;  // cells per tick
    bool  hardCannon           = true;   // roaming cactus cannon
    float cannonBallSpeed      = 1.35f;  // cells per tick
    int   cannonFireMinMs      = 1700;
    int   cannonFireMaxMs      = 2700;
    bool  hardSpeedRamp        = true;   // the world speeds up as the score rises
    int   speedRampEveryScore  = 50;     // score step between speed-ups
    float speedRampFirstStep   = 0.2f;   // first speed-up
    float speedRampStep        = 0.15f;  // every later speed-up
    bool  hardFlashEvent       = true;   // blinding white flash
    int   flashEveryMs         = 150000;
    int   flashDurationMs      = 1000;

    // ===== Troll mode features =====
    int   trollEventMinMs      = 2200;   // time between random troll events
    int   trollEventMaxMs      = 5200;
    bool  trollFakePipes       = true;   // look-alike pipes that turn solid
    int   fakePipeChancePct    = 28;
    bool  trollWind            = true;   // sudden sideways gusts
    float windStrength         = 7.6f;
    bool  trollFakeGap         = true;   // the gap briefly jumps
    int   fakeGapShiftCells    = 6;
    int   fakeGapDurationMs    = 650;
    bool  trollPipeLunge       = true;   // pipes suddenly jump toward you
    int   pipeLungeChancePct   = 35;
    int   pipeLungeMinCells    = 4;
    int   pipeLungeMaxCells    = 10;
    bool  trollBirdResize      = true;   // bird becomes huge / tiny for a while
    int   resizeMinMs          = 1800;
    int   resizeMaxMs          = 4200;
    bool  trollFakeGameOver    = true;   // fake GAME OVER screen
    int   fakeGameOverChancePct = 18;
    int   fakeGameOverMinScore = 3;
    bool  trollFallingLetters  = true;   // title letters fall and hurt
};

// Clamp values and fix inconsistent pairs (min <= max, red <= green <= max...).
void sanitizeConfig(GameConfig &c);

// Presets shown in the dialog. Index 0 = defaults.
QStringList configPresetNames();
void applyConfigPreset(GameConfig &c, int index);

// Persist the settings between runs (QSettings).
void loadConfig(GameConfig &c);
void saveConfig(const GameConfig &c);

// Description of one editable field. The dialog, the reset button and the
// save / load code are all generated from this table.
struct FieldDesc
{
    enum Kind { Int, Float, Bool, Enum };
    Kind kind;
    const char *tab;
    const char *key;
    const char *label;
    const char *tip;
    int   GameConfig::*ip;
    float GameConfig::*fp;
    bool  GameConfig::*bp;
    double minV, maxV, step;
    QStringList choices;       // Enum only (the value is stored in an int member)
};
const std::vector<FieldDesc> &configFields();

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // offerStart = true adds a "Start multiplayer" button.
    SettingsDialog(GameConfig &config, bool offerStart, QWidget *parent = nullptr);
    bool startRequested() const { return startClicked; }

private:
    struct Binding { const FieldDesc *desc; QWidget *widget; };

    GameConfig &target;
    std::vector<Binding> bindings;
    QComboBox *presetBox = nullptr;
    bool startClicked = false;

    void build(bool offerStart);
    void showConfig(const GameConfig &c);
    GameConfig readConfig() const;
    void commit(bool start);
};

#endif // GAMESETTINGS_H
