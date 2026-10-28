#include "gamesettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

// ---------------------------------------------------------------------------
// Field table: one line per setting. Order = order in the dialog.
// ---------------------------------------------------------------------------
#define F_INT(tab, m, label, tip, lo, hi, st) \
    { FieldDesc::Int, tab, #m, label, tip, &GameConfig::m, nullptr, nullptr, lo, hi, st, {} }
#define F_FLT(tab, m, label, tip, lo, hi, st) \
    { FieldDesc::Float, tab, #m, label, tip, nullptr, &GameConfig::m, nullptr, lo, hi, st, {} }
#define F_BOOL(tab, m, label, tip) \
    { FieldDesc::Bool, tab, #m, label, tip, nullptr, nullptr, &GameConfig::m, 0, 1, 1, {} }
#define F_ENUM(tab, m, label, tip, ...) \
    { FieldDesc::Enum, tab, #m, label, tip, &GameConfig::m, nullptr, nullptr, 0, 0, 1, QStringList{ __VA_ARGS__ } }

const std::vector<FieldDesc> &configFields()
{
    static const std::vector<FieldDesc> fields = {
        // ---------------- Multiplayer ----------------
        F_INT ("Multiplayer", mpPlayerCount, "Players", "Number of birds on this keyboard (2-4).", 2, 4, 1),
        F_ENUM("Multiplayer", mpMatchType, "Match type",
               "Versus: last bird standing wins.  Co-op: everyone shares one score and the run ends when all birds are out.",
               "Versus (last bird standing)", "Co-op (shared score)"),
        F_BOOL("Multiplayer", mpHardMode, "Hard-mode features", "Plant eaters, cannon, speed ramp and flash (configure them on the Hard tab)."),
        F_BOOL("Multiplayer", mpTrollMode, "Troll-mode features", "Fake pipes, wind, fake game over... (configure them on the Troll tab)."),
        F_INT ("Multiplayer", mpTargetScore, "Versus: score to win (0 = off)", "First bird to reach this score wins immediately.", 0, 9999, 1),
        F_INT ("Multiplayer", livesPerPlayer, "Lives per player", "Crashes allowed before a bird is out. Also applies to single player.", 1, 9, 1),
        F_INT ("Multiplayer", respawnInvulnMs, "Respawn protection (ms)", "Invulnerable time after losing a life.", 0, 10000, 100),
        F_ENUM("Multiplayer", mpBirdCollision, "Bird vs bird", "Pass through each other, or bump and bounce off.",
               "Pass through", "Bump"),
        F_FLT ("Multiplayer", mpHorizontalSpeed, "Thrust / brake push", "Multiplayer: how fast a bird moves across the screen relative to Player 1 (the reference frame) when their speeds differ.", 0.0, 5.0, 0.1),
        F_FLT ("Multiplayer", mpReturnSpring, "Return-to-start pull", "How strongly Player 1 is pulled back to its start column after wind or bumps (0 = never). Other birds keep their offset.", 0.0, 0.3, 0.005),
        F_INT ("Multiplayer", mpGameOverLockMs, "Game-over key lock (ms)", "Prevents an accidental instant restart.", 0, 5000, 100),

        // ---------------- Pipes ----------------
        F_INT ("Pipes", pipeGap, "Pipe gap (cells)", "Height of the opening you fly through.", 12, 120, 1),
        F_INT ("Pipes", pipeSpacing, "Pipe spacing (cells)", "Distance between two pipes.", 20, 250, 1),
        F_INT ("Pipes", pipeWidth, "Pipe width (cells)", "Thickness of a pipe.", 3, 40, 1),
        F_FLT ("Pipes", pipeSpeed, "Bird / scroll speed", "How fast the world scrolls (cells per tick).", 0.2, 5.0, 0.05),
        F_INT ("Pipes", pipeGapMargin, "Gap margin (cells)", "Closest the gap may get to the ceiling or ground.", 0, 30, 1),
        F_FLT ("Pipes", pipeGapShrinkPerPipe, "Gap shrink per pipe", "Each new pipe has a smaller gap by this amount.", 0.0, 3.0, 0.05),
        F_INT ("Pipes", pipeGapMin, "Smallest gap (cells)", "The shrinking gap stops here.", 10, 120, 1),

        // ---------------- Bird physics ----------------
        F_FLT ("Bird", gravity, "Gravity", "Downward acceleration per tick.", 0.0, 2.0, 0.01),
        F_FLT ("Bird", flapStrength, "Flap strength", "Upward impulse of a flap.", 0.5, 10.0, 0.1),
        F_FLT ("Bird", maxFallSpeed, "Max fall speed", "Terminal velocity.", 1.0, 20.0, 0.5),
        F_INT ("Bird", birdSize, "Bird size (cells)", "Hit-box size; the sprite scales with it.", 2, 12, 1),

        // ---------------- Speed ----------------
        F_FLT ("Speed", thrustAccel, "Acceleration (Right)", "Speed gained per tick while thrusting.", 0.0, 0.5, 0.005),
        F_FLT ("Speed", brakeDecel, "Deceleration (Left)", "Speed lost per tick while braking.", 0.0, 0.5, 0.005),
        F_FLT ("Speed", idleDrag, "Idle drag", "How quickly speed returns to normal when no key is held.", 0.0, 0.5, 0.005),
        F_FLT ("Speed", speedMulMin, "Minimum speed multiplier", "Braking limit.", 0.1, 1.0, 0.05),
        F_FLT ("Speed", speedMulMax, "Maximum speed multiplier", "Thrust limit.", 1.0, 5.0, 0.05),

        // ---------------- Energy ----------------
        F_BOOL("Energy", unlimitedEnergy, "Unlimited energy", "Flap, thrust and brake cost nothing."),
        F_FLT ("Energy", energyMax, "Energy capacity", "", 20.0, 500.0, 5.0),
        F_FLT ("Energy", energyGreenMin, "Green zone starts at", "", 0.0, 500.0, 5.0),
        F_FLT ("Energy", energyRedMax, "Red zone ends at", "Below this the bird is exhausted and cannot act.", 0.0, 500.0, 5.0),
        F_FLT ("Energy", flapCost, "Flap cost", "", 0.0, 50.0, 0.5),
        F_FLT ("Energy", thrustCost, "Thrust cost / tick", "", 0.0, 10.0, 0.1),
        F_FLT ("Energy", brakeCost, "Brake cost / tick", "", 0.0, 10.0, 0.1),
        F_FLT ("Energy", energyRegen, "Regeneration / tick", "", 0.0, 10.0, 0.05),

        // ---------------- Feather blades / cannon ----------------
        F_BOOL("Blades", bladesEnabled, "Feather blades enabled", "Cut holes through pipes and shoot the cannon."),
        F_BOOL("Blades", startWithBlade, "Start with a blade", ""),
        F_INT ("Blades", bladeGap, "Hole height (cells)", "Size of the hole a blade cuts in a pipe.", 4, 80, 1),
        F_FLT ("Blades", bladeSpeed, "Blade speed", "Cells per tick.", 0.5, 12.0, 0.25),
        F_INT ("Blades", bladeLength, "Blade length (cells)", "Drawn length.", 1, 10, 1),
        F_INT ("Blades", bladeScoreInterval, "Score per new blade", "", 1, 200, 1),
        F_INT ("Blades", cannonHitsToKill, "Hits to destroy cannon", "", 1, 20, 1),

        // ---------------- Hard mode ----------------
        F_FLT ("Hard", hardSpeedBonus, "Extra scroll speed", "Added to the pipe speed in hard mode.", 0.0, 3.0, 0.05),
        F_BOOL("Hard", hardPlantEaters, "Plant eaters", "Plants lunge out of the pipes."),
        F_FLT ("Hard", eaterTriggerDistance, "Eater trigger distance", "", 4.0, 60.0, 1.0),
        F_FLT ("Hard", eaterMaxExtension, "Eater reach (cells)", "", 1.0, 30.0, 0.5),
        F_FLT ("Hard", eaterGrowSpeed, "Eater lunge speed", "", 0.05, 3.0, 0.05),
        F_FLT ("Hard", eaterRetractSpeed, "Eater retract speed", "", 0.05, 3.0, 0.05),
        F_BOOL("Hard", hardCannon, "Cactus cannon", "A roaming cannon that shoots at the birds."),
        F_FLT ("Hard", cannonBallSpeed, "Cannonball speed", "", 0.2, 6.0, 0.05),
        F_INT ("Hard", cannonFireMinMs, "Fire interval min (ms)", "", 200, 20000, 100),
        F_INT ("Hard", cannonFireMaxMs, "Fire interval max (ms)", "", 200, 20000, 100),
        F_BOOL("Hard", hardSpeedRamp, "Speed ramp", "The world speeds up as the score grows."),
        F_INT ("Hard", speedRampEveryScore, "Ramp every N points", "", 1, 500, 1),
        F_FLT ("Hard", speedRampFirstStep, "First speed-up", "", 0.0, 3.0, 0.05),
        F_FLT ("Hard", speedRampStep, "Each later speed-up", "", 0.0, 3.0, 0.05),
        F_BOOL("Hard", hardFlashEvent, "White flash event", "Periodic blinding flash."),
        F_INT ("Hard", flashEveryMs, "Flash every (ms)", "", 2000, 600000, 1000),
        F_INT ("Hard", flashDurationMs, "Flash duration (ms)", "", 100, 10000, 100),

        // ---------------- Troll mode ----------------
        F_INT ("Troll", trollEventMinMs, "Event interval min (ms)", "Time between random troll events.", 300, 60000, 100),
        F_INT ("Troll", trollEventMaxMs, "Event interval max (ms)", "", 300, 60000, 100),
        F_BOOL("Troll", trollFakePipes, "Fake pipes", "Pale look-alike pipes that turn solid at the last moment."),
        F_INT ("Troll", fakePipeChancePct, "Fake pipe chance (%)", "", 0, 100, 1),
        F_BOOL("Troll", trollWind, "Wind gusts", "Sudden sideways shove."),
        F_FLT ("Troll", windStrength, "Wind strength", "", 0.5, 20.0, 0.5),
        F_BOOL("Troll", trollFakeGap, "Moving gap", "The gap of a nearby pipe briefly jumps."),
        F_INT ("Troll", fakeGapShiftCells, "Gap jump (cells)", "", 1, 30, 1),
        F_INT ("Troll", fakeGapDurationMs, "Gap jump duration (ms)", "", 100, 5000, 50),
        F_BOOL("Troll", trollPipeLunge, "Pipe lunge", "A pipe suddenly jumps toward the bird."),
        F_INT ("Troll", pipeLungeChancePct, "Lunging pipe chance (%)", "", 0, 100, 1),
        F_INT ("Troll", pipeLungeMinCells, "Lunge min (cells)", "", 1, 40, 1),
        F_INT ("Troll", pipeLungeMaxCells, "Lunge max (cells)", "", 1, 40, 1),
        F_BOOL("Troll", trollBirdResize, "Bird resize", "The bird becomes huge or tiny for a while."),
        F_INT ("Troll", resizeMinMs, "Resize duration min (ms)", "", 200, 30000, 100),
        F_INT ("Troll", resizeMaxMs, "Resize duration max (ms)", "", 200, 30000, 100),
        F_BOOL("Troll", trollFakeGameOver, "Fake game over", "A fake GAME OVER screen freezes the game briefly."),
        F_INT ("Troll", fakeGameOverChancePct, "Fake game over chance (%)", "", 0, 100, 1),
        F_INT ("Troll", fakeGameOverMinScore, "Only after score", "", 0, 100, 1),
        F_BOOL("Troll", trollFallingLetters, "Falling title letters", "Letters drop from the heading and hurt."),

        // ---------------- Audio ----------------
        F_BOOL("Audio", musicEnabled, "Background music", "Play relaxing background music while you play."),
        F_FLT ("Audio", musicVolume, "Music volume", "Background music volume (0 = silent, 1 = full).", 0.0, 1.0, 0.05),
    };
    return fields;
}

// ---------------------------------------------------------------------------
void sanitizeConfig(GameConfig &c)
{
    c.mpPlayerCount = std::clamp(c.mpPlayerCount, 2, 4);
    c.livesPerPlayer = std::max(1, c.livesPerPlayer);
    c.pipeGap = std::max(12, c.pipeGap);
    c.pipeGapMin = std::min(c.pipeGapMin, c.pipeGap);
    c.birdSize = std::clamp(c.birdSize, 2, 12);
    c.speedMulMin = std::min(c.speedMulMin, 1.0f);
    c.speedMulMax = std::max(c.speedMulMax, 1.0f);
    c.energyMax = std::max(1.0f, c.energyMax);
    c.energyGreenMin = std::min(c.energyGreenMin, c.energyMax);
    c.energyRedMax = std::min(c.energyRedMax, c.energyGreenMin);
    if (c.cannonFireMaxMs <= c.cannonFireMinMs) c.cannonFireMaxMs = c.cannonFireMinMs + 1;
    if (c.trollEventMaxMs <= c.trollEventMinMs) c.trollEventMaxMs = c.trollEventMinMs + 1;
    if (c.resizeMaxMs <= c.resizeMinMs) c.resizeMaxMs = c.resizeMinMs + 1;
    if (c.pipeLungeMaxCells <= c.pipeLungeMinCells) c.pipeLungeMaxCells = c.pipeLungeMinCells + 1;
    c.musicVolume = std::clamp(c.musicVolume, 0.0f, 1.0f);
}

QStringList configPresetNames()
{
    return { "Default", "Chill", "Brutal", "Moon gravity", "Chaos" };
}

void applyConfigPreset(GameConfig &c, int index)
{
    // Presets keep the multiplayer choices (players, match type...) the user made.
    GameConfig keep = c;
    c = GameConfig();
    c.mpPlayerCount = keep.mpPlayerCount;
    c.mpMatchType = keep.mpMatchType;
    c.mpTargetScore = keep.mpTargetScore;
    c.mpBirdCollision = keep.mpBirdCollision;
    c.mpHardMode = keep.mpHardMode;
    c.mpTrollMode = keep.mpTrollMode;
    c.musicEnabled = keep.musicEnabled;   // presets never touch the audio settings
    c.musicVolume = keep.musicVolume;

    switch (index) {
    case 1: // Chill
        c.pipeGap = 70; c.pipeSpacing = 85; c.pipeSpeed = 0.9f;
        c.gravity = 0.24f; c.flapCost = 1.5f; c.energyRegen = 0.5f;
        c.livesPerPlayer = 3;
        break;
    case 2: // Brutal
        c.pipeGap = 36; c.pipeSpacing = 50; c.pipeSpeed = 1.7f;
        c.gravity = 0.36f; c.flapCost = 5.0f; c.energyRegen = 0.15f;
        c.pipeGapShrinkPerPipe = 0.3f; c.pipeGapMin = 28;
        break;
    case 3: // Moon gravity
        c.gravity = 0.10f; c.flapStrength = 2.0f; c.maxFallSpeed = 3.0f;
        break;
    case 4: // Chaos
        c.mpHardMode = true; c.mpTrollMode = true;
        c.trollEventMinMs = 900; c.trollEventMaxMs = 2500;
        c.fakePipeChancePct = 50; c.windStrength = 10.0f; c.fakeGameOverChancePct = 30;
        c.cannonFireMinMs = 900; c.cannonFireMaxMs = 1600;
        c.livesPerPlayer = 3;
        break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
void loadConfig(GameConfig &c)
{
    QSettings s(QStringLiteral("BirdsVsPlants"), QStringLiteral("BirdsVsPlants"));
    for (const FieldDesc &d : configFields()) {
        const QString key = QStringLiteral("config/") + QLatin1String(d.key);
        if (!s.contains(key)) continue;
        switch (d.kind) {
        case FieldDesc::Int:
        case FieldDesc::Enum: c.*(d.ip) = s.value(key).toInt(); break;
        case FieldDesc::Float: c.*(d.fp) = s.value(key).toFloat(); break;
        case FieldDesc::Bool: c.*(d.bp) = s.value(key).toBool(); break;
        }
    }
    sanitizeConfig(c);
}

void saveConfig(const GameConfig &c)
{
    QSettings s(QStringLiteral("BirdsVsPlants"), QStringLiteral("BirdsVsPlants"));
    for (const FieldDesc &d : configFields()) {
        const QString key = QStringLiteral("config/") + QLatin1String(d.key);
        switch (d.kind) {
        case FieldDesc::Int:
        case FieldDesc::Enum: s.setValue(key, c.*(d.ip)); break;
        case FieldDesc::Float: s.setValue(key, c.*(d.fp)); break;
        case FieldDesc::Bool: s.setValue(key, c.*(d.bp)); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------
SettingsDialog::SettingsDialog(GameConfig &config, bool offerStart, QWidget *parent)
    : QDialog(parent), target(config)
{
    setWindowTitle(offerStart ? tr("Multiplayer setup") : tr("Game settings"));
    resize(560, 620);
    build(offerStart);
    showConfig(target);
}

void SettingsDialog::build(bool offerStart)
{
    auto *root = new QVBoxLayout(this);

    // Preset row.
    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(tr("Preset:")));
    presetBox = new QComboBox;
    presetBox->addItems(configPresetNames());
    presetRow->addWidget(presetBox, 1);
    auto *applyPreset = new QPushButton(tr("Load preset"));
    presetRow->addWidget(applyPreset);
    root->addLayout(presetRow);
    connect(applyPreset, &QPushButton::clicked, this, [this]() {
        GameConfig c = readConfig();
        applyConfigPreset(c, presetBox->currentIndex());
        showConfig(c);
    });

    // One tab per group, each with a form of generated editors.
    auto *tabs = new QTabWidget;
    root->addWidget(tabs, 1);

    QMap<QString, QFormLayout *> forms;
    for (const FieldDesc &d : configFields()) {
        const QString tab = QString::fromLatin1(d.tab);
        if (!forms.contains(tab)) {
            auto *page = new QWidget;
            auto *form = new QFormLayout(page);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            tabs->addTab(scroll, tab);
            forms.insert(tab, form);

            if (tab == QLatin1String("Multiplayer")) {
                auto *help = new QLabel(tr(
                    "<b>Controls</b> (flap / brake / thrust / feather)<br>"
                    "P1: Space or click / Left / Right / Alt<br>"
                    "P2: W / A / D / S<br>"
                    "P3: I / J / L / K<br>"
                    "P4: T / F / H / G"));
                help->setTextFormat(Qt::RichText);
                form->addRow(help);
            }
        }

        QWidget *w = nullptr;
        switch (d.kind) {
        case FieldDesc::Int: {
            auto *sb = new QSpinBox;
            sb->setRange((int)d.minV, (int)d.maxV);
            sb->setSingleStep((int)d.step);
            w = sb;
            break;
        }
        case FieldDesc::Float: {
            auto *sb = new QDoubleSpinBox;
            sb->setDecimals(3);
            sb->setRange(d.minV, d.maxV);
            sb->setSingleStep(d.step);
            w = sb;
            break;
        }
        case FieldDesc::Bool:
            w = new QCheckBox;
            break;
        case FieldDesc::Enum: {
            auto *cb = new QComboBox;
            cb->addItems(d.choices);
            w = cb;
            break;
        }
        }
        w->setToolTip(QString::fromUtf8(d.tip));
        forms[tab]->addRow(QString::fromUtf8(d.label), w);
        bindings.push_back({ &d, w });
    }

    // Buttons.
    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(tr("Reset to defaults"));
    auto *cancel = new QPushButton(tr("Cancel"));
    auto *save = new QPushButton(offerStart ? tr("Save && close") : tr("Save"));
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    if (offerStart) {
        auto *start = new QPushButton(tr("Start multiplayer"));
        start->setDefault(true);
        buttons->addWidget(start);
        connect(start, &QPushButton::clicked, this, [this]() { commit(true); });
    } else {
        save->setDefault(true);
    }
    root->addLayout(buttons);

    connect(reset, &QPushButton::clicked, this, [this]() { showConfig(GameConfig()); });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this]() { commit(false); });
}

void SettingsDialog::showConfig(const GameConfig &c)
{
    for (const Binding &b : bindings) {
        const FieldDesc &d = *b.desc;
        switch (d.kind) {
        case FieldDesc::Int:
            static_cast<QSpinBox *>(b.widget)->setValue(c.*(d.ip));
            break;
        case FieldDesc::Float:
            static_cast<QDoubleSpinBox *>(b.widget)->setValue(c.*(d.fp));
            break;
        case FieldDesc::Bool:
            static_cast<QCheckBox *>(b.widget)->setChecked(c.*(d.bp));
            break;
        case FieldDesc::Enum:
            static_cast<QComboBox *>(b.widget)->setCurrentIndex(c.*(d.ip));
            break;
        }
    }
}

GameConfig SettingsDialog::readConfig() const
{
    GameConfig c = target;
    for (const Binding &b : bindings) {
        const FieldDesc &d = *b.desc;
        switch (d.kind) {
        case FieldDesc::Int:
            c.*(d.ip) = static_cast<QSpinBox *>(b.widget)->value();
            break;
        case FieldDesc::Float:
            c.*(d.fp) = (float)static_cast<QDoubleSpinBox *>(b.widget)->value();
            break;
        case FieldDesc::Bool:
            c.*(d.bp) = static_cast<QCheckBox *>(b.widget)->isChecked();
            break;
        case FieldDesc::Enum:
            c.*(d.ip) = qMax(0, static_cast<QComboBox *>(b.widget)->currentIndex());
            break;
        }
    }
    return c;
}

void SettingsDialog::commit(bool start)
{
    GameConfig c = readConfig();
    sanitizeConfig(c);
    target = c;
    saveConfig(target);
    startClicked = start;
    accept();
}
