#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPolygonF>
#include <QResizeEvent>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QInputDialog>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QHostAddress>
#include <QPushButton>
#include <QLineEdit>
#include <QStringList>
#include <QFontMetrics>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <algorithm>
#include <cmath>


namespace {
QJsonObject gameConfigToJson(const GameConfig &config)
{
    QJsonObject object;
    for (const FieldDesc &field : configFields()) {
        const QString key = QString::fromLatin1(field.key);
        switch (field.kind) {
        case FieldDesc::Int:
        case FieldDesc::Enum: object.insert(key, config.*(field.ip)); break;
        case FieldDesc::Float: object.insert(key, config.*(field.fp)); break;
        case FieldDesc::Bool: object.insert(key, config.*(field.bp)); break;
        }
    }
    return object;
}

void gameConfigFromJson(GameConfig &config, const QJsonObject &object)
{
    for (const FieldDesc &field : configFields()) {
        const QString key = QString::fromLatin1(field.key);
        if (!object.contains(key)) continue;
        switch (field.kind) {
        case FieldDesc::Int:
        case FieldDesc::Enum: config.*(field.ip) = object.value(key).toInt(config.*(field.ip)); break;
        case FieldDesc::Float: config.*(field.fp) = (float)object.value(key).toDouble(config.*(field.fp)); break;
        case FieldDesc::Bool: config.*(field.bp) = object.value(key).toBool(config.*(field.bp)); break;
        }
    }
    sanitizeConfig(config);
}

// Host-to-joinee state snapshots are capped at roughly 30 Hz. The threshold sits a
// little below the 33 ms game tick so ordinary timer jitter can never make the host
// skip a snapshot it should have sent.
constexpr int LanSnapshotMinIntervalMs = 25;
// A joinee blends from the previous to the newest snapshot over this long.
constexpr int LanSnapshotIntervalMs = 33;

// QMediaPlayer streams from a real file, so tracks bundled in the resources are
// copied into the per-user cache folder first. Returns the local file path, or an
// empty string if it could not be provided.
QString extractResourceToCache(const QString &resourcePath, const QString &fileName)
{
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dirPath.isEmpty() || !QDir().mkpath(dirPath))
        dirPath = QDir::tempPath();
    const QString target = QDir(dirPath).filePath(fileName);

    // Files copied out of the resources are read-only, and Windows refuses to delete
    // a read-only file, so clear that flag before replacing last run's copy.
    if (QFile::exists(target)) {
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner | QFileDevice::WriteUser);
        QFile::remove(target);
    }
    if (QFile::copy(resourcePath, target)) {
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::WriteOwner | QFileDevice::WriteUser);
        return target;
    }

    // The old copy could not be replaced, e.g. a second running copy of the game is
    // playing it right now. A file of identical size is the same bundled asset.
    const QFileInfo existing(target);
    if (existing.exists() && existing.size() == QFileInfo(resourcePath).size())
        return target;
    return QString();
}

}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    state(IntroWait),
    gridW(100), gridH(100),
    pipeSpeed(1.2f), pipeWidth(12),
    pipeGap(50), pipeSpacing(65),
    distSinceLastPipe(0),
    groundH(3), groundOffset(0),
    score(0), highScore(0),
    hardMode(false),
    trollMode(false),
    bobTimer(0),
    comicPanel(0),
    comicMs(0),
    comicVideoPlayer(new QMediaPlayer(this)),
    comicVideoSink(new QVideoSink(this)),
    comicVideoFrame(),
    comicTransitionMs(0),
    comicVideoStarted(false),
    typewriterCharsShown(0),
    typewriterTick(0),
    typewriterWait(0),
    headingText(QStringLiteral("                     FLAPPY BIRD -V.3.0.1")),
    headingDropDelay(90),
    headingRefillSlot(-1),
    headingRefillT(1.0f),
    headingRefillWait(0),
    cannonX(0),
    cannonY(0),
    cannonVelX(0),
    cannonVelY(0),
    cannonAimX(1),
    cannonAimY(0),
    cannonFireMs(0),
    cannonDirectionMs(0),
    hardPlayMs(0),
    flashMsLeft(0),
    trollPauseMs(0),
    trollEventMs(0),
    windX(0),
    windAnimMs(0),
    fakeGapMs(0)
{
    // Setup the window UI and attach input slots to the custom label.
    ui->setupUi(this);

    lanNetwork = new LanNetwork(this);
    connect(lanNetwork, &LanNetwork::statusChanged, this, [this](const QString &message) {
        ui->statusLabel->setText(message);
    });
    connect(lanNetwork, &LanNetwork::connectedToPeer, this, [this]() {
        if (!lanNetwork->isHost()) {
            lanClientMode = true;
            ui->statusLabel->setText(QStringLiteral("Connected to host — receiving match settings…"));
            return;
        }
        const int expectedRemotePlayers = qBound(1, cfg.mpPlayerCount - 1, 3);
        if (lanNetwork->peerCount() < expectedRemotePlayers) {
            ui->statusLabel->setText(QStringLiteral("LAN lobby: %1/%2 players connected. Waiting…")
                                     .arg(lanNetwork->peerCount() + 1).arg(cfg.mpPlayerCount));
            return;
        }
        if (!lanMatchActive) {
            lanMatchActive = true;
            lanClientMode = false;
            beginPlay(cfg.mpHardMode, cfg.mpTrollMode, true);
            ui->statusLabel->setText(QStringLiteral("LAN match started — host is Player 1"));
        }
        broadcastLanState();
        ui->frame->setFocus();
    });
    connect(lanNetwork, &LanNetwork::messageReceived, this, &MainWindow::handleLanMessage);
    connect(lanNetwork, &LanNetwork::disconnectedFromPeer, this, [this](int playerId) {
        if (!lanMatchActive) {
            if (!lanNetwork->isHost()) {
                lanClientMode = false;
                lanLocalPlayerId = 0;
                ui->statusLabel->setText(QStringLiteral("LAN connection closed."));
            }
            return;
        }
        if (lanNetwork->isHost()) {
            if (playerId >= 1 && playerId < (int)birds.size()) {
                birds[playerId].alive = false;
                birds[playerId].lives = 0;
                checkMatchEnd();
            }
            ui->statusLabel->setText(QStringLiteral("A LAN player disconnected; the host simulation continues."));
            broadcastLanState();
            return;
        }
        lanMatchActive = false;
        lanClientMode = false;
        lanLocalPlayerId = 0;
        if (state == Playing || state == GameOver) showModeSelect();
        ui->statusLabel->setText(QStringLiteral("Disconnected from LAN host."));
    });

    loadConfig(cfg);          // settings saved by the dialog in an earlier run
    birds.assign(1, Bird());

    initMusic();

    comicVideoPlayer->setVideoOutput(comicVideoSink);
    comicVideoPlayer->setSource(QUrl(QStringLiteral("qrc:/comic/assets/after_zero_cinematic.mp4")));
    comicVideoPlayer->setLoops(1);
    connect(comicVideoSink, &QVideoSink::videoFrameChanged, this, &MainWindow::onComicVideoFrameChanged);
    connect(comicVideoPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            finishComicVideo();
    });
    // If the cinematic cannot be decoded, skip it instead of hanging on a black screen.
    connect(comicVideoPlayer, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &message) {
        qWarning() << "Comic video playback failed:" << error << message;
        comicVideoReady = false;
        if (state == ComicVideo)
            finishComicVideo();
    });

    // Dedicated opening music. The loopable bed plays from app startup through the
    // full comic/video/title-card sequence, then showModeSelect() hands off to the
    // normal looping background track.
    introAudioPlayer = new QMediaPlayer(this);
    introAudioOutput = new QAudioOutput(this);
    introAudioPlayer->setAudioOutput(introAudioOutput);
    introAudioPlayer->setLoops(QMediaPlayer::Infinite);
    introAudioOutput->setVolume(0.58f);

    // Short, low-latency accents keep the soundtrack locked to the comic's actual
    // transitions, even when the player advances panels manually.
    panelTransitionSound = new QSoundEffect(this);
    panelTransitionSound->setSource(QUrl(QStringLiteral("qrc:/music/assets/panel_transition.wav")));
    panelTransitionSound->setLoopCount(1);
    panelTransitionSound->setVolume(0.24f);

    videoRevealSound = new QSoundEffect(this);
    videoRevealSound->setSource(QUrl(QStringLiteral("qrc:/music/assets/video_reveal.wav")));
    videoRevealSound->setLoopCount(1);
    videoRevealSound->setVolume(0.36f);

    titleRevealSound = new QSoundEffect(this);
    titleRevealSound->setSource(QUrl(QStringLiteral("qrc:/music/assets/title_reveal.wav")));
    titleRevealSound->setLoopCount(1);
    titleRevealSound->setVolume(0.28f);

    // A clear, ominous impact plays exactly when a bird crashes.
    gameOverCrashSound = new QSoundEffect(this);
    gameOverCrashSound->setSource(QUrl(QStringLiteral("qrc:/music/assets/game_over_crash.wav")));
    gameOverCrashSound->setLoopCount(1);
    gameOverCrashSound->setVolume(0.78f);

    const QString introAudioPath = extractResourceToCache(QStringLiteral(":/music/assets/intro_cinematic.ogg"),
                                                          QStringLiteral("intro_cinematic.ogg"));
    if (!introAudioPath.isEmpty())
        introAudioPlayer->setSource(QUrl::fromLocalFile(introAudioPath));
    else
        qWarning() << "Could not copy bundled intro cinematic audio into app cache.";
    applyMusicSettings();

    comicPanels = {
        QPixmap(QStringLiteral(":/comic/assets/panel1.png")),
        QPixmap(QStringLiteral(":/comic/assets/panel2.png")),
        QPixmap(QStringLiteral(":/comic/assets/panel3.png")),
        QPixmap(QStringLiteral(":/comic/assets/panel4.png")),
        QPixmap(QStringLiteral(":/comic/assets/panel5.png"))
    };

    headingInPlace.assign(headingText.size(), 1);

    ui->easyButton->hide();
    ui->hardButton->hide();
    ui->trollButton->hide();
    ui->multiplayerButton->hide();
    ui->localMultiplayerButton->hide();
    ui->hostLanButton->hide();
    ui->joinLanButton->hide();
    ui->multiplayerBackButton->hide();
    ui->settingsButton->hide();
    ui->restartButton->hide();

    connect(ui->frame, &my_label::flap, this, &MainWindow::onFlap);
    connect(ui->frame, &my_label::shoot, this, &MainWindow::onShoot);
    connect(ui->frame, &my_label::thrustHeld, this, &MainWindow::onThrustHeld);
    connect(ui->frame, &my_label::brakeHeld, this, &MainWindow::onBrakeHeld);
    connect(ui->easyButton, &QPushButton::clicked, this, &MainWindow::onEasyClicked);
    connect(ui->hardButton, &QPushButton::clicked, this, &MainWindow::onHardClicked);
    connect(ui->trollButton, &QPushButton::clicked, this, &MainWindow::onTrollClicked);
    connect(ui->multiplayerButton, &QPushButton::clicked, this, &MainWindow::onMultiplayerClicked);
    connect(ui->localMultiplayerButton, &QPushButton::clicked, this, &MainWindow::onLocalMultiplayerClicked);
    connect(ui->hostLanButton, &QPushButton::clicked, this, &MainWindow::onHostLanClicked);
    connect(ui->joinLanButton, &QPushButton::clicked, this, &MainWindow::onJoinLanClicked);
    connect(ui->multiplayerBackButton, &QPushButton::clicked, this, &MainWindow::onMultiplayerBackClicked);
    connect(ui->settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->restartButton, &QPushButton::clicked, this, &MainWindow::onRestartClicked);

    gameTimer = new QTimer(this);
    connect(gameTimer, &QTimer::timeout, this, &MainWindow::updateGame);

    // Main game loop tick. TICK_MS controls the frame rate of the game.
    // 33 ms ~= 30 FPS, so the world updates about 30 times each second.
    updateGridSize();
    resetGame();

    ui->frame->setFocus();
    ui->statusLabel->setText("SPACE to begin");
    gameTimer->start(TICK_MS);
    renderFrame();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateGridSize();
    if (state == IntroWait || state == ComicIntro || state == ComicBlackBeforeVideo || state == ComicVideo || state == ComicWhiteFade || state == Typewriter || state == ModeSelect) {
        renderFrame();
    }
}

void MainWindow::initMusic()
{
    // Background music: one calm looping track bundled in the resources.
    musicPlayer = new QMediaPlayer(this);
    musicOutput = new QAudioOutput(this);
    musicPlayer->setAudioOutput(musicOutput);
    musicPlayer->setLoops(QMediaPlayer::Infinite);

    const QString musicPath = extractResourceToCache(QStringLiteral(":/music/assets/background_music.ogg"),
                                                     QStringLiteral("background_music.ogg"));
    if (!musicPath.isEmpty())
        musicPlayer->setSource(QUrl::fromLocalFile(musicPath));
    else
        qWarning() << "Could not copy bundled background music into app cache.";
    applyMusicSettings();
}

void MainWindow::applyMusicSettings()
{
    const bool inOpeningSequence = state == IntroWait || state == ComicIntro ||
                                   state == ComicBlackBeforeVideo || state == ComicVideo ||
                                   state == ComicWhiteFade || state == Typewriter;
    const bool audioEnabled = cfg.musicEnabled && cfg.musicVolume > 0.0f;

    // The cinematic bed owns the entire opening sequence, including the initial
    // prompt, comic panels, video, fade and title card. It is stopped only when
    // the mode-selection menu is ready; the normal looping track takes over then.
    if (introAudioPlayer && introAudioOutput) {
        introAudioOutput->setVolume(audioEnabled ? qBound(0.0f, cfg.musicVolume, 1.0f) * 0.58f : 0.0f);
        if (audioEnabled && inOpeningSequence) {
            if (introAudioPlayer->playbackState() != QMediaPlayer::PlayingState)
                introAudioPlayer->play();
        } else if (introAudioPlayer->playbackState() == QMediaPlayer::PlayingState) {
            introAudioPlayer->stop();
            introAudioPlayer->setPosition(0);
        }
    }

    if (!musicPlayer || !musicOutput) return;
    musicOutput->setVolume(qBound(0.0f, cfg.musicVolume, 1.0f));
    const bool shouldPlay = audioEnabled && !inOpeningSequence;
    if (shouldPlay) {
        if (musicPlayer->playbackState() != QMediaPlayer::PlayingState) musicPlayer->play();
    } else if (musicPlayer->playbackState() == QMediaPlayer::PlayingState) {
        musicPlayer->pause();
    }
}

void MainWindow::playComicTransitionCue(QSoundEffect *effect, float gain)
{
    if (!effect || !cfg.musicEnabled || cfg.musicVolume <= 0.0f)
        return;

    // Use the same master volume switch as music, but keep these accents lower
    // than the soundtrack so they enhance transitions instead of overpowering it.
    effect->stop();
    effect->setVolume(qBound(0.0f, cfg.musicVolume, 1.0f) * gain);
    effect->play();
}

void MainWindow::playGameOverCrash()
{
    if (!gameOverCrashSound || !cfg.musicEnabled || cfg.musicVolume <= 0.0f)
        return;

    // Stop/retrigger so every new crash gets the full, noticeable sting.
    gameOverCrashSound->stop();
    gameOverCrashSound->setVolume(qBound(0.0f, cfg.musicVolume, 1.0f) * 0.78f);
    gameOverCrashSound->play();
}

void MainWindow::updateGridSize()
{
    // A LAN client draws the HOST's world, so it keeps the host's grid size and
    // only scales the finished picture to its own window (see my_label::paintEvent).
    if (lanClientMode && lanMatchActive) return;

    const int previousGridW = gridW;
    const int previousGridH = gridH;
    int pw = ui->frame->width();
    int ph = ui->frame->height();
    if (pw < CELL) pw = CELL;
    if (ph < CELL) ph = CELL;
    gridW = pw / CELL;
    gridH = ph / CELL;
    homeX = gridW / 5;
    if (homeX < 3) homeX = 3;
    // Single player: the bird's column follows the window (original behaviour).
    if (!multiplayer)
        for (auto &b : birds) b.x = (float)homeX;

    // Joinees only receive the world size in lan_init, so tell them when resizing
    // the host's window changes it in the middle of a match.
    if ((gridW != previousGridW || gridH != previousGridH) &&
        lanMatchActive && lanNetwork && lanNetwork->isHost())
        sendLanInit();
}

void MainWindow::applyDifficulty()
{
    pipeGap = cfg.pipeGap;
    pipeWidth = cfg.pipeWidth;
    pipeSpacing = cfg.pipeSpacing;
    pipeSpeed = cfg.pipeSpeed + (hardMode ? cfg.hardSpeedBonus : 0.0f);
}

void MainWindow::updateHardSpeed()
{
    if (!hardMode || !cfg.hardSpeedRamp) return;
    int steps = score / qMax(1, cfg.speedRampEveryScore);
    float base = cfg.pipeSpeed + cfg.hardSpeedBonus;
    if (steps <= 0)
        pipeSpeed = base;
    else
        pipeSpeed = base + cfg.speedRampFirstStep + (steps - 1) * cfg.speedRampStep;
}

float MainWindow::refSpeedMul() const
{
    // The camera / reference frame follows Player 1 (birds[0]). If Player 1 is
    // out in multiplayer, the world falls back to cruising speed.
    if (birds.empty()) return 1.0f;
    if (!multiplayer || birds[0].alive) return birds[0].speedMul;
    return 1.0f;
}

float MainWindow::worldSpeed() const
{
    // The world scrolls at Player 1's speed, so Player 1 always feels the
    // thrust / brake directly. In multiplayer the other birds are then moved
    // across the screen by their speed *relative to Player 1*.
    return pipeSpeed * refSpeedMul();
}

void MainWindow::showModeSelect()
{
    updateGridSize();   // back to this device's own grid after leaving a LAN match
    state = ModeSelect;
    ui->easyButton->show();
    ui->hardButton->show();
    ui->trollButton->show();
    ui->multiplayerButton->show();
    ui->localMultiplayerButton->hide();
    ui->hostLanButton->hide();
    ui->joinLanButton->hide();
    ui->multiplayerBackButton->hide();
    ui->settingsButton->show();
    ui->restartButton->hide();
    ui->statusLabel->setText("Choose a mode");
    ui->frame->setFocus();
    renderFrame();
    // The menu has now been prepared and rendered: stop/reset intro music and
    // hand over to the normal looping background track.
    applyMusicSettings();
}

void MainWindow::beginPlay(bool hard, bool troll, bool multi)
{
    hardMode = hard;
    trollMode = troll;
    multiplayer = multi;
    state = Playing;
    ui->easyButton->hide();
    ui->hardButton->hide();
    ui->trollButton->hide();
    ui->multiplayerButton->hide();
    ui->localMultiplayerButton->hide();
    ui->hostLanButton->hide();
    ui->joinLanButton->hide();
    ui->multiplayerBackButton->hide();
    ui->settingsButton->hide();
    ui->restartButton->hide();
    QString label = trollMode ? "Troll Mode" : (hardMode ? "Hard" : "Easy");
    if (multiplayer)
        label = QString("%1 players - %2%3%4").arg(cfg.mpPlayerCount)
                    .arg(cfg.mpMatchType == 1 ? "Co-op" : "Versus")
                    .arg(hardMode ? " + Hard" : "").arg(trollMode ? " + Troll" : "");
    ui->statusLabel->setText(label);
    resetGame();
    lanCountdownMs = (lanMatchActive && lanNetwork && lanNetwork->isHost()) ? LanCountdownMs : 0;
    ui->frame->setFocus();
    // Send the static match settings and world size once per round; the periodic
    // state snapshots no longer repeat them.
    if (lanMatchActive && lanNetwork && lanNetwork->isHost())
        sendLanInit();
}

void MainWindow::onEasyClicked()
{
    if (state == ModeSelect)
        beginPlay(false);
}

void MainWindow::onHardClicked()
{
    if (state == ModeSelect)
        beginPlay(true, false);
}

void MainWindow::onTrollClicked()
{
    if (state == ModeSelect)
        beginPlay(false, true);
}

void MainWindow::onMultiplayerClicked()
{
    if (state != ModeSelect) return;

    showMultiplayerOptions();
}

void MainWindow::showMultiplayerOptions()
{
    ui->easyButton->hide();
    ui->hardButton->hide();
    ui->trollButton->hide();
    ui->multiplayerButton->hide();
    ui->settingsButton->hide();
    ui->localMultiplayerButton->show();
    ui->hostLanButton->show();
    ui->joinLanButton->show();
    ui->multiplayerBackButton->show();
    ui->statusLabel->setText(QStringLiteral("Choose a multiplayer option"));
    ui->frame->setFocus();
}

void MainWindow::onLocalMultiplayerClicked()
{
    if (state != ModeSelect) return;
    SettingsDialog dlg(cfg, true, this);
    const bool accepted = (dlg.exec() == QDialog::Accepted);
    applyMusicSettings();
    if (accepted && dlg.startRequested())
        beginPlay(cfg.mpHardMode, cfg.mpTrollMode, true);
    else
        ui->frame->setFocus();
}

void MainWindow::onHostLanClicked()
{
    if (state != ModeSelect) return;
    SettingsDialog dlg(cfg, true, this);
    const bool hostAccepted = (dlg.exec() == QDialog::Accepted);
    applyMusicSettings();
    if (!hostAccepted || !dlg.startRequested()) {
        ui->frame->setFocus();
        return;
    }
    cfg.mpPlayerCount = qBound(2, cfg.mpPlayerCount, my_label::MAX_PLAYERS);
    if (lanNetwork->startHost(45454, cfg.mpPlayerCount - 1)) {
        lanMatchActive = false;
        lanClientMode = false;
        lanLocalPlayerId = 0;
        QStringList addresses;
        const auto interfaces = QNetworkInterface::allAddresses();
        for (const QHostAddress &address : interfaces) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                !address.isLoopback() && address != QHostAddress::AnyIPv4)
                addresses << address.toString();
        }
        addresses.removeDuplicates();
        const QString addressText = addresses.isEmpty()
            ? QStringLiteral("Find this computer's IPv4 address in network settings.")
            : addresses.join(QStringLiteral("\n"));
        QMessageBox::information(this, QStringLiteral("Host LAN game"),
            QStringLiteral("Hosting on port 45454.\n\nGive the other players one of these local IP addresses:\n%1\n\nPlayers must be on the same Wi-Fi/LAN. Allow the game through the firewall if prompted.\n\nMode: %2. Waiting for %3 remote player(s).")
            .arg(addressText)
            .arg(cfg.mpTrollMode ? QStringLiteral("Troll") : (cfg.mpHardMode ? QStringLiteral("Hard") : QStringLiteral("Easy")))
            .arg(cfg.mpPlayerCount - 1));
        ui->statusLabel->setText(QStringLiteral("LAN lobby: 1/%1 players. Waiting for connections…").arg(cfg.mpPlayerCount));
    }
    ui->frame->setFocus();
}

void MainWindow::onJoinLanClicked()
{
    if (state != ModeSelect) return;
    bool ok = false;
    const QString address = QInputDialog::getText(this, QStringLiteral("Join LAN game"),
        QStringLiteral("Enter the host's local IPv4 address:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !address.isEmpty()) {
        lanClientMode = true;
        lanNetwork->joinHost(address, 45454);
    }
    ui->frame->setFocus();
}

void MainWindow::onMultiplayerBackClicked()
{
    if (state != ModeSelect) return;
    showModeSelect();
}

void MainWindow::onSettingsClicked()
{
    if (state != ModeSelect) return;
    SettingsDialog dlg(cfg, false, this);
    dlg.exec();
    applyMusicSettings();
    ui->frame->setFocus();
}

void MainWindow::onRestartClicked()
{
    if (lanMatchActive) {
        if (lanClientMode) sendLanInput(QStringLiteral("restart"));
        else if (state == GameOver) beginPlay(hardMode, trollMode, true);
        return;
    }
    showModeSelect();
}

void MainWindow::skipOrAdvanceComic()
{
    if (state == ComicIntro) {
        playComicTransitionCue(panelTransitionSound, 0.28f);
        comicPanel++;
        comicMs = 0;
        if (comicPanel >= (int)comicPanels.size()) {
            state = ComicBlackBeforeVideo;
            comicTransitionMs = 0;
            comicVideoStarted = false;
        }
    } else if (state == ComicBlackBeforeVideo) {
        startComicVideo();
    } else if (state == ComicVideo) {
        finishComicVideo();
    } else if (state == ComicWhiteFade) {
        state = Typewriter;
        typewriterCharsShown = 0;
        typewriterTick = 0;
        typewriterWait = 0;
    } else if (state == Typewriter) {
        typewriterCharsShown = QStringLiteral("Can Jero survive?").size();
        typewriterWait = 2;
    }
    renderFrame();
}

void MainWindow::startComicVideo()
{
    comicTransitionMs = 0;
    if (!comicVideoReady) {
        // The cinematic could not be loaded: go straight to the title card.
        playComicTransitionCue(titleRevealSound, 0.30f);
        state = ComicWhiteFade;
        renderFrame();
        return;
    }

    playComicTransitionCue(videoRevealSound, 0.42f);
    comicVideoStarted = true;
    comicVideoFrame = QVideoFrame();
    comicVideoPlayer->setPosition(0);
    comicVideoPlayer->play();
    state = ComicVideo;
    applyMusicSettings();   // keeps the intro bed running and pauses normal BGM
}

void MainWindow::finishComicVideo()
{
    if (!comicVideoStarted && state != ComicVideo)
        return;
    comicVideoPlayer->pause();
    playComicTransitionCue(titleRevealSound, 0.32f);
    comicVideoStarted = false;
    comicTransitionMs = 0;
    state = ComicWhiteFade;
    applyMusicSettings();   // keep intro music through the fade and title card
}

void MainWindow::onComicVideoFrameChanged(const QVideoFrame &frame)
{
    comicVideoFrame = frame;
    if (state == ComicVideo)
        renderFrame();
}

void MainWindow::onFlap(int player)
{
    if (state == Playing && lanCountdownMs > 0) return;   // "get ready" hold
    if (lanMatchActive && lanNetwork && lanNetwork->isHost() && player != 0 && !dispatchingLanInput) return;
    if (lanMatchActive && lanClientMode) {
        if (state == GameOver) sendLanInput(QStringLiteral("restart"));
        else if (state == Playing && player == 0) sendLanInput(QStringLiteral("flap"));
        return;
    }
    if (state == IntroWait) {
        state = ComicIntro;
        comicPanel = 0;
        comicMs = 0;
        // Continue the cinematic bed already playing from application startup.
        applyMusicSettings();
        ui->statusLabel->setText("SPACE next");
        renderFrame();
    } else if (state == ComicIntro || state == ComicBlackBeforeVideo || state == ComicVideo || state == ComicWhiteFade || state == Typewriter) {
        // Story screens: only player 1's key skips.
        if (player == 0)
            skipOrAdvanceComic();
    } else if (state == ModeSelect) {
        return;
    } else if (state == Playing) {
        if (player < 0 || player >= (int)birds.size()) return;
        Bird &b = birds[player];
        // Flapping costs energy and is locked while the bar is in the red zone.
        if (!b.alive || trollPauseMs > 0 || !canAct(b)) return;
        b.vel = -cfg.flapStrength;
        b.flapAnimMs = FLAP_ANIM_MS;
        if (!cfg.unlimitedEnergy) {
            b.energy -= cfg.flapCost;
            if (b.energy < 0) b.energy = 0;
        }
    } else if (state == GameOver) {
        if (multiplayer && gameOverMs < cfg.mpGameOverLockMs) return;
        beginPlay(hardMode, trollMode, multiplayer);
    }
}

void MainWindow::onThrustHeld(int player, bool held)
{
    if (lanMatchActive && lanNetwork && lanNetwork->isHost() && player != 0 && !dispatchingLanInput) return;
    if (lanMatchActive && lanClientMode) {
        if (player == 0) sendLanInput(QStringLiteral("thrust"), held);
        return;
    }
    if (player >= 0 && player < (int)birds.size()) birds[player].thrustKey = held;
}

void MainWindow::onBrakeHeld(int player, bool held)
{
    if (lanMatchActive && lanNetwork && lanNetwork->isHost() && player != 0 && !dispatchingLanInput) return;
    if (lanMatchActive && lanClientMode) {
        if (player == 0) sendLanInput(QStringLiteral("brake"), held);
        return;
    }
    if (player >= 0 && player < (int)birds.size()) birds[player].brakeKey = held;
}

void MainWindow::updateEnergy(Bird &b)
{
    // Right = accelerate, Left = reverse flap (brake). Both drain energy and
    // are locked in the red zone. Pressing both cancels out and costs nothing.
    bool thrust = b.thrustKey && !b.brakeKey && canAct(b);
    bool brake  = b.brakeKey && !b.thrustKey && canAct(b);

    if (thrust) {
        b.speedMul += cfg.thrustAccel;
        if (!cfg.unlimitedEnergy) b.energy -= cfg.thrustCost;
    } else if (brake) {
        b.speedMul -= cfg.brakeDecel;
        if (!cfg.unlimitedEnergy) b.energy -= cfg.brakeCost;
    } else {
        // Drift back toward normal cruising speed.
        if (b.speedMul > 1.0f) b.speedMul = qMax(1.0f, b.speedMul - cfg.idleDrag);
        else if (b.speedMul < 1.0f) b.speedMul = qMin(1.0f, b.speedMul + cfg.idleDrag);
    }
    b.thrustActive = thrust;
    b.brakeActive = brake;
    if (b.speedMul > cfg.speedMulMax) b.speedMul = cfg.speedMulMax;
    if (b.speedMul < cfg.speedMulMin) b.speedMul = cfg.speedMulMin;

    // Energy recovers slowly whenever the bird isn't actively thrusting/braking.
    if (!thrust && !brake)
        b.energy += cfg.energyRegen;
    if (b.energy > cfg.energyMax) b.energy = cfg.energyMax;
    if (b.energy < 0) b.energy = 0;
    if (cfg.unlimitedEnergy) b.energy = cfg.energyMax;
}

void MainWindow::updateBirdAnim(Bird &b)
{
    // Wing beats are faster while thrusting / braking, gentle otherwise.
    b.wingPhase += (b.thrustActive || b.brakeActive) ? 0.95f : 0.35f;
    if (b.wingPhase > 6.2831853f) b.wingPhase -= 6.2831853f;
    if (b.flapAnimMs > 0)  b.flapAnimMs  = qMax(0, b.flapAnimMs  - TICK_MS);
    if (b.shootAnimMs > 0) b.shootAnimMs = qMax(0, b.shootAnimMs - TICK_MS);

    // Body pitch follows vertical speed, plus a lean forward for thrust and
    // a nose-up flare when braking.
    float target = b.vel * 5.0f;
    if (target > 40.0f) target = 40.0f;
    if (target < -28.0f) target = -28.0f;
    if (b.thrustActive) target += 12.0f;
    if (b.brakeActive)  target -= 22.0f;
    b.tilt += (target - b.tilt) * 0.35f;
}

void MainWindow::onShoot(int player)
{
    if (state == Playing && lanCountdownMs > 0) return;   // "get ready" hold
    if (lanMatchActive && lanNetwork && lanNetwork->isHost() && player != 0 && !dispatchingLanInput) return;
    if (lanMatchActive && lanClientMode) {
        if (player == 0) sendLanInput(QStringLiteral("shoot"));
        return;
    }
    // Fire the stocked feather blade. Only one blade may be in flight per bird.
    if (state != Playing || trollPauseMs > 0 || !cfg.bladesEnabled) return;
    if (player < 0 || player >= (int)birds.size()) return;
    Bird &bird = birds[player];
    if (!bird.alive || !bird.hasBlade || !bird.blades.empty()) return;

    FeatherBlade b;
    b.x = (float)(birdCol(bird) + birdW(bird));
    b.y = bird.y + birdH(bird) / 2.0f;
    bird.blades.push_back(b);
    bird.hasBlade = false;
    bird.shootAnimMs = SHOOT_ANIM_MS;
    // The next blade comes N points after the score at the moment of firing.
    bird.nextBladeScore = bird.score + cfg.bladeScoreInterval;
}

void MainWindow::tryAwardBlade(Bird &b)
{
    // At most one blade in stock; none while one is flying.
    if (!cfg.bladesEnabled) return;
    if (b.hasBlade || !b.blades.empty()) return;
    if (b.score >= b.nextBladeScore)
        b.hasBlade = true;
}

void MainWindow::refreshScore()
{
    // `score` is the number that drives the HUD, the speed ramp and the best score.
    if (multiplayer && cfg.mpMatchType == 1) {
        score = coopScore;
    } else {
        score = 0;
        for (const auto &b : birds) score = qMax(score, b.score);
    }
    ui->scoreLabel->setText("Score: " + QString::number(score));
}

void MainWindow::respawnBird(Bird &b)
{
    // Put the bird back in the middle of the nearest pipe gap ahead (or the
    // middle of the screen) so it does not respawn inside an obstacle.
    const int playableH = gridH - groundH;
    float targetY = (playableH - birdH(b)) / 2.0f;
    const Pipe *best = nullptr;
    for (const auto &p : pipes) {
        if (p.x + pipeWidth < b.x) continue;
        if (!best || p.x < best->x) best = &p;
    }
    if (best) targetY = best->gapTop + best->gapHeight / 2.0f - birdH(b) / 2.0f;
    b.y = qBound(0.0f, targetY, (float)(playableH - birdH(b)));
    b.vel = 0;
    b.invulnMs = cfg.respawnInvulnMs;
    b.energy = cfg.energyMax;
    b.speedMul = 1.0f;
}

void MainWindow::killBird(Bird &b)
{
    b.lives--;
    if (b.lives > 0) {
        respawnBird(b);
    } else {
        b.alive = false;
        b.thrustKey = b.brakeKey = false;
        b.thrustActive = b.brakeActive = false;
    }
}

void MainWindow::resetGame()
{
    // Reset the gameplay state for a fresh run.
    // This keeps the birds, pipes, score and animation states consistent.
    updateGridSize();
    applyDifficulty();

    const int count = multiplayer ? qBound(2, cfg.mpPlayerCount, my_label::MAX_PLAYERS) : 1;
    static const int hues[my_label::MAX_PLAYERS] = { 0, 170, 300, 80 };
    birds.assign(count, Bird());
    const int playableH = gridH - groundH;
    for (int i = 0; i < count; i++) {
        Bird &b = birds[i];
        b.id = i;
        b.hue = hues[i];
        b.scale = 1;
        b.x = (float)homeX;
        if (count == 1)
            b.y = playableH / 2.0f;
        else    // spread the birds over the screen height
            b.y = playableH * (i + 1) / (float)(count + 1) - birdH(b) / 2.0f;
        b.energy = cfg.energyMax;
        b.speedMul = 1.0f;
        b.hasBlade = cfg.bladesEnabled && cfg.startWithBlade;
        b.nextBladeScore = cfg.bladeScoreInterval;
        b.lives = qMax(1, cfg.livesPerPlayer);
        b.alive = true;
    }

    pipes.clear();
    fallingLetters.clear();
    cactusBalls.clear();
    cannonHits = 0;
    cannonAlive = cfg.hardCannon;
    cannonX = (gridW - CANNON_W) * 0.68f;
    cannonY = (gridH - groundH - CANNON_H) * 0.52f;
    cannonVelX = QRandomGenerator::global()->bounded(2) ? 0.35f : -0.35f;
    cannonVelY = QRandomGenerator::global()->bounded(2) ? 0.24f : -0.24f;
    cannonAimX = -1.0f;
    cannonAimY = 0.0f;
    cannonFireMs = QRandomGenerator::global()->bounded(1200, 1900);
    cannonDirectionMs = QRandomGenerator::global()->bounded(900, 1800);
    headingInPlace.assign(headingText.size(), 1);
    headingDropDelay = QRandomGenerator::global()->bounded(60, 140);
    headingRefillSlot = -1;
    headingRefillT = 1.0f;
    headingRefillWait = 0;
    hardPlayMs = 0;
    flashMsLeft = 0;
    trollPauseMs = 0;
    trollEventMs = QRandomGenerator::global()->bounded(90, 180);
    windX = 0;
    windAnimMs = 0;
    fakeGapMs = 0;
    distSinceLastPipe = pipeSpacing;
    pipesSpawned = 0;
    coopScore = 0;
    winner = -1;
    gameOverMs = 0;
    playMs = 0;
    score = 0;
    groundOffset = 0;
    bobTimer = 0;
    ui->scoreLabel->setText("Score: 0");
}

void MainWindow::updateGame()
{
    // LAN clients never simulate gameplay locally: the host owns physics/collisions.
    if (lanMatchActive && lanClientMode) {
        // Snapshots arrive at ~30 Hz. Blend the visual bird/pipe positions between the
        // previous and the latest authoritative host positions; no client-side
        // physics/collisions run.
        lanInterpolationElapsedMs = qMin(LanSnapshotIntervalMs, lanInterpolationElapsedMs + TICK_MS);
        const float alpha = qBound(0.0f, lanInterpolationElapsedMs / (float)LanSnapshotIntervalMs, 1.0f);
        const size_t count = qMin(birds.size(), qMin(lanPreviousBirdPositions.size(), lanTargetBirdPositions.size()));
        for (size_t i = 0; i < count; ++i) {
            birds[i].x = lanPreviousBirdPositions[i].first
                + (lanTargetBirdPositions[i].first - lanPreviousBirdPositions[i].first) * alpha;
            birds[i].y = lanPreviousBirdPositions[i].second
                + (lanTargetBirdPositions[i].second - lanPreviousBirdPositions[i].second) * alpha;
        }
        // Pipe ordering is stable between spawn/removal snapshots. Interpolate only
        // when the counts match so a newly spawned pipe is never blended with a
        // different pipe that shifted into its index.
        if (pipes.size() == lanPreviousPipeX.size() && pipes.size() == lanTargetPipeX.size()) {
            for (size_t i = 0; i < pipes.size(); ++i)
                pipes[i].x = lanPreviousPipeX[i] + (lanTargetPipeX[i] - lanPreviousPipeX[i]) * alpha;
        }
        renderFrame();
        return;
    }
    // Main game loop tick. This updates physics, scoring, enemies and screen animation.
    updateGridSize();

    if (state == ComicIntro) {
        comicMs += TICK_MS;
        if (comicMs >= COMIC_PANEL_MS) {
            playComicTransitionCue(panelTransitionSound, 0.28f);
            comicMs = 0;
            comicPanel++;
            if (comicPanel >= (int)comicPanels.size()) {
                state = ComicBlackBeforeVideo;
                comicTransitionMs = 0;
                comicVideoStarted = false;
            }
        }
    } else if (state == ComicBlackBeforeVideo) {
        comicTransitionMs += TICK_MS;
        if (comicTransitionMs >= COMIC_BLACK_MS)
            startComicVideo();
    } else if (state == ComicVideo) {
        // QMediaPlayer advances the cinematic. The EndOfMedia callback moves to the white flash.
    } else if (state == ComicWhiteFade) {
        comicTransitionMs += TICK_MS;
        if (comicTransitionMs >= COMIC_WHITE_MS + COMIC_WHITE_FADE_MS) {
            state = Typewriter;
            typewriterCharsShown = 0;
            typewriterTick = 0;
            typewriterWait = 0;
        }
    } else if (state == Typewriter) {
        const QString question = QStringLiteral("Can Jero survive?");
        if (typewriterCharsShown < question.size()) {
            typewriterTick++;
            if (typewriterTick >= TYPEWRITER_TICKS_PER_CHAR) {
                typewriterTick = 0;
                typewriterCharsShown++;
            }
        } else {
            typewriterWait += TICK_MS;
            if (typewriterWait >= 2000)
                showModeSelect();
        }
    } else if (state == ModeSelect) {
        bobTimer += 0.1f;
        groundOffset += 0.5f;
        bgScroll += 0.5;
        if (groundOffset >= 4.0f) groundOffset -= 4.0f;
    } else if (state == GameOver) {
        gameOverMs += TICK_MS;
    } else if (state == Playing) {
        // "Get ready" hold at the start of a LAN round: nothing moves yet.
        if (lanCountdownMs > 0) {
            lanCountdownMs = qMax(0, lanCountdownMs - TICK_MS);
            if (lanMatchActive && lanNetwork && lanNetwork->isHost()) broadcastLanState();
            renderFrame();
            return;
        }
        if (trollMode && trollPauseMs > 0) {
            trollPauseMs -= TICK_MS;
            if (trollPauseMs < 0) trollPauseMs = 0;
            if (trollPauseMs == 0)
                ui->statusLabel->setText("TROLL MODE");
            if (lanMatchActive && lanNetwork && lanNetwork->isHost()) broadcastLanState();
            renderFrame();
            return;
        }

        playMs += TICK_MS;
        const int playableH = gridH - groundH;

        // ---- Bird physics (every living bird) ----
        for (auto &b : birds) {
            if (!b.alive) continue;
            updateEnergy(b);
            updateBirdAnim(b);
            if (b.invulnMs > 0) b.invulnMs = qMax(0, b.invulnMs - TICK_MS);

            b.vel += cfg.gravity;
            if (b.vel > cfg.maxFallSpeed) b.vel = cfg.maxFallSpeed;
            b.y += b.vel;

            if (multiplayer) {
                // Reference frame = Player 1. The world scrolls at Player 1's
                // speed; every other bird drifts across the screen by its speed
                // relative to Player 1 (it can pass Player 1 or fall behind) and
                // keeps that offset until its speed changes again. Only Player 1
                // is gently pulled back to its start column (after wind / bumps).
                if (b.id == 0 && birds[0].alive)
                    b.x += ((float)homeX - b.x) * cfg.mpReturnSpring;
                else
                    b.x += (b.speedMul - refSpeedMul()) * pipeSpeed * cfg.mpHorizontalSpeed;
                const float maxX = qMax(2.0f, gridW - 2.0f - birdW(b));
                b.x = qBound(2.0f, b.x, maxX);
            }
        }

        if (trollMode) {
            updateTroll();
            updateHeadingLetters();
            for (auto &b : birds) {
                if (!b.alive) continue;
                b.x += std::round(windX);
                if (b.x < 1) b.x = 1;
                int maxX = gridW - birdW(b) - 1;
                if (b.x > maxX) b.x = (float)qMax(1, maxX);
            }
        }

        if (multiplayer && cfg.mpBirdCollision == 1)
            resolveBirdBumps();

        for (auto &b : birds) {
            if (b.y < 0) {
                b.y = 0;
                b.vel = 0;
            }
        }

        // ---- World scroll ----
        const float speed = worldSpeed();
        for (auto &p : pipes) {
            p.x -= speed;
        }

        while (!pipes.empty() && pipes.front().x + pipeWidth < 0) {
            pipes.erase(pipes.begin());
        }

        distSinceLastPipe += speed;
        if (distSinceLastPipe >= pipeSpacing) {
            spawnPipe();
            distSinceLastPipe = 0;
        }

        // ---- Scoring: each bird scores a pipe once, when it clears it ----
        bool scoredNow = false;
        for (auto &p : pipes) {
            for (auto &b : birds) {
                if (!b.alive) continue;
                const unsigned bit = 1u << b.id;
                if (p.scoredMask & bit) continue;
                if ((p.x + pipeWidth) < birdCol(b)) {
                    p.scoredMask |= bit;
                    b.score++;
                    scoredNow = true;
                    if (!p.scored) {
                        p.scored = true;
                        coopScore++;
                    }
                }
            }
        }
        if (scoredNow) {
            refreshScore();
            updateHardSpeed();
            for (auto &b : birds) tryAwardBlade(b);
        }

        groundOffset += speed;
        bgScroll += speed;
        if (groundOffset >= 4.0f) groundOffset -= 4.0f;

        if (hardMode) {
            updateEaters();
            updateHardCannon();
            if (cfg.hardFlashEvent) {
                hardPlayMs += TICK_MS;
                if (hardPlayMs >= cfg.flashEveryMs) {
                    hardPlayMs -= cfg.flashEveryMs;
                    flashMsLeft = cfg.flashDurationMs;
                }
            }
            if (flashMsLeft > 0)
                flashMsLeft -= TICK_MS;
        }

        for (auto &b : birds) updateBlades(b);

        // ---- Crashes ----
        for (auto &b : birds) {
            if (!b.alive) continue;
            if (b.invulnMs > 0) {
                // Protected birds cannot sink through the ground.
                float maxY = (float)(playableH - birdH(b));
                if (b.y > maxY) { b.y = maxY; b.vel = 0; }
                continue;
            }
            if (checkCollision(b)) {
                b.diedAtMs = playMs;
                killBird(b);
            }
        }
        checkMatchEnd();
    }

    if (lanMatchActive && lanNetwork && lanNetwork->isHost())
        broadcastLanState();
    renderFrame();
}

void MainWindow::resolveBirdBumps()
{
    // Simple axis-aligned bump between living birds.
    for (size_t i = 0; i < birds.size(); i++) {
        for (size_t j = i + 1; j < birds.size(); j++) {
            Bird &a = birds[i];
            Bird &b = birds[j];
            if (!a.alive || !b.alive) continue;
            const float aw = (float)birdW(a), ah = (float)birdH(a);
            const float bw = (float)birdW(b), bh = (float)birdH(b);
            const float ox = qMin(a.x + aw, b.x + bw) - qMax(a.x, b.x);
            const float oy = qMin(a.y + ah, b.y + bh) - qMax(a.y, b.y);
            if (ox <= 0 || oy <= 0) continue;
            if (ox < oy) {
                const float push = ox / 2.0f + 0.1f;
                if (a.x < b.x) { a.x -= push; b.x += push; }
                else           { a.x += push; b.x -= push; }
            } else {
                const float push = oy / 2.0f + 0.1f;
                if (a.y < b.y) { a.y -= push; b.y += push; }
                else           { a.y += push; b.y -= push; }
                std::swap(a.vel, b.vel);
            }
        }
    }
}

void MainWindow::checkMatchEnd()
{
    int alive = 0, last = -1;
    for (const auto &b : birds) {
        if (b.alive) { alive++; last = b.id; }
    }

    bool over = false;
    winner = -1;

    if (!multiplayer || cfg.mpMatchType == 1) {
        over = (alive == 0);
    } else {
        // Versus: first to the target score, or last bird standing.
        if (cfg.mpTargetScore > 0) {
            for (const auto &b : birds) {
                if (b.score >= cfg.mpTargetScore && (winner < 0 || b.score > birds[winner].score)) {
                    winner = b.id;
                    over = true;
                }
            }
        }
        if (!over && alive <= 1) {
            over = true;
            if (alive == 1) {
                winner = last;
            } else {
                // Everyone is out: best score wins, then whoever lasted longer.
                // An exact tie on both is a draw.
                for (const auto &b : birds) {
                    if (winner < 0) { winner = b.id; continue; }
                    const Bird &w = birds[winner];
                    if (b.score > w.score || (b.score == w.score && b.diedAtMs > w.diedAtMs))
                        winner = b.id;
                    else if (b.score == w.score && b.diedAtMs == w.diedAtMs)
                        winner = -2;   // tie marker, resolved below
                }
                if (winner == -2) winner = -1;
            }
        }
    }

    if (over)
        startGameOver();
}

void MainWindow::startGameOver()
{
    state = GameOver;
    gameOverMs = 0;
    refreshScore();
    if (score > highScore) {
        highScore = score;
        ui->highScoreLabel->setText("Best: " + QString::number(highScore));
    }
    playGameOverCrash();
    ui->restartButton->show();
    ui->statusLabel->setText("GAME OVER!\nSPACE to retry\nRestart to choose mode");
}

void MainWindow::spawnPipe()
{
    // Create a new obstacle column. The gap position changes randomly,
    // so each pipe forces a different path through the level.
    Pipe p;
    p.x = (float)gridW;
    int playableH = gridH - groundH;
    int gap = pipeGap;
    if (cfg.pipeGapShrinkPerPipe > 0.0f)
        gap = qMax(cfg.pipeGapMin, (int)std::lround(pipeGap - cfg.pipeGapShrinkPerPipe * pipesSpawned));
    gap = qMin(gap, qMax(4, playableH - 2));
    pipesSpawned++;
    int minGapTop = cfg.pipeGapMargin;
    int maxGapTop = playableH - gap - cfg.pipeGapMargin;
    if (maxGapTop < minGapTop) maxGapTop = minGapTop;
    p.gapTop = QRandomGenerator::global()->bounded(minGapTop, maxGapTop + 1);
    p.gapHeight = gap;
    p.scored = false;
    p.scoredMask = 0;
    p.hasEater = hardMode && cfg.hardPlantEaters;
    // Troll pipes can be harmless decoys that turn solid near the bird.
    p.fakePipe = trollMode && cfg.trollFakePipes && (QRandomGenerator::global()->bounded(100) < cfg.fakePipeChancePct);
    p.fakeActivated = false;
    p.gapShifted = false;
    p.originalGapTop = p.gapTop;
    // Some Troll pipes can lunge forward during a random event.
    p.windAffected = trollMode && cfg.trollPipeLunge && (QRandomGenerator::global()->bounded(100) < cfg.pipeLungeChancePct);
    p.eaterFromTop = QRandomGenerator::global()->bounded(2) == 0;
    p.eaterTriggered = false;
    p.eaterOut = 0;
    p.seed = QRandomGenerator::global()->bounded(1, 1 << 30);
    p.eaterMax = cfg.eaterMaxExtension;
    if (p.eaterMax > p.gapHeight - 2)
        p.eaterMax = (float)qMax(2, p.gapHeight - 2);
    pipes.push_back(p);
}

void MainWindow::updateEaters()
{
    for (auto &p : pipes) {
        if (!p.hasEater) continue;
        // Triggered by the closest living bird in front of the pipe; the plant
        // keeps attacking while any bird is still beside / in front of it.
        float nearestCol = 1e9f;       // column of the nearest bird ahead
        float rearCol = 1e9f;          // rear-most living bird (decides retracting)
        for (const auto &b : birds) {
            if (!b.alive) continue;
            const float col = (float)birdCol(b);
            if (p.x - col > 0.0f && p.x - col < nearestCol - 0.0f) nearestCol = p.x - col;
            rearCol = qMin(rearCol, col);
        }
        if (!p.eaterTriggered && nearestCol < cfg.eaterTriggerDistance) {
            p.eaterTriggered = true;
        }
        if (p.eaterTriggered) {
            if (p.x + pipeWidth > rearCol - 4)
                p.eaterOut += cfg.eaterGrowSpeed;
            else
                p.eaterOut -= cfg.eaterRetractSpeed;
            if (p.eaterOut > p.eaterMax) p.eaterOut = p.eaterMax;
            if (p.eaterOut < 0) p.eaterOut = 0;
        }
    }
}

void MainWindow::updateHardCannon()
{
    if (!hardMode || !cfg.hardCannon) return;

    if (!cannonAlive) {
        // The cannon is gone, but balls already in flight keep moving.
        for (auto &ball : cactusBalls) {
            ball.x += ball.vx;
            ball.y += ball.vy;
        }
        cactusBalls.erase(
            std::remove_if(cactusBalls.begin(), cactusBalls.end(), [this](const CactusBall &ball) {
                return ball.x + CACTUS_BALL_RADIUS < 0 ||
                       ball.x - CACTUS_BALL_RADIUS > gridW ||
                       ball.y + CACTUS_BALL_RADIUS < 0 ||
                       ball.y - CACTUS_BALL_RADIUS > gridH - groundH;
            }),
            cactusBalls.end());
        return;
    }

    int playableH = gridH - groundH;
    float maxX = qMax(1, gridW - CANNON_W - 1);
    float maxY = qMax(2, playableH - CANNON_H - 1);

    cannonDirectionMs -= TICK_MS;
    if (cannonDirectionMs <= 0) {
        cannonVelX = (QRandomGenerator::global()->bounded(2) ? 1.0f : -1.0f) *
                     QRandomGenerator::global()->bounded(20, 46) / 100.0f;
        cannonVelY = (QRandomGenerator::global()->bounded(2) ? 1.0f : -1.0f) *
                     QRandomGenerator::global()->bounded(12, 34) / 100.0f;
        cannonDirectionMs = QRandomGenerator::global()->bounded(900, 2200);
    }

    cannonX += cannonVelX;
    cannonY += cannonVelY;
    if (cannonX < 1) {
        cannonX = 1;
        cannonVelX = std::fabs(cannonVelX);
    } else if (cannonX > maxX) {
        cannonX = maxX;
        cannonVelX = -std::fabs(cannonVelX);
    }
    if (cannonY < 2) {
        cannonY = 2;
        cannonVelY = std::fabs(cannonVelY);
    } else if (cannonY > maxY) {
        cannonY = maxY;
        cannonVelY = -std::fabs(cannonVelY);
    }

    cannonFireMs -= TICK_MS;
    if (cannonFireMs <= 0) {
        float cannonCenterX = cannonX + CANNON_W / 2.0f;
        float cannonCenterY = cannonY + CANNON_H / 2.0f;
        // Aim at the nearest living bird.
        float dx = 0, dy = 0, distance = 1e9f;
        bool haveTarget = false;
        for (const auto &b : birds) {
            if (!b.alive) continue;
            float tx = birdCol(b) + birdW(b) / 2.0f - cannonCenterX;
            float ty = b.y + birdH(b) / 2.0f - cannonCenterY;
            float d = std::sqrt(tx * tx + ty * ty);
            if (d < distance) { distance = d; dx = tx; dy = ty; haveTarget = true; }
        }
        if (haveTarget && distance > 0.0f) {
            cannonAimX = dx / distance;
            cannonAimY = dy / distance;
        }

        CactusBall ball;
        float muzzleDistance = CANNON_W / 2.0f + 0.8f;
        ball.x = cannonCenterX + cannonAimX * muzzleDistance;
        ball.y = cannonCenterY + cannonAimY * muzzleDistance;
        ball.vx = cannonAimX * cfg.cannonBallSpeed;
        ball.vy = cannonAimY * cfg.cannonBallSpeed;
        cactusBalls.push_back(ball);
        cannonFireMs = QRandomGenerator::global()->bounded(cfg.cannonFireMinMs, cfg.cannonFireMaxMs);
    }

    for (auto &ball : cactusBalls) {
        ball.x += ball.vx;
        ball.y += ball.vy;
    }
    cactusBalls.erase(
        std::remove_if(cactusBalls.begin(), cactusBalls.end(), [this](const CactusBall &ball) {
            return ball.x + CACTUS_BALL_RADIUS < 0 ||
                   ball.x - CACTUS_BALL_RADIUS > gridW ||
                   ball.y + CACTUS_BALL_RADIUS < 0 ||
                   ball.y - CACTUS_BALL_RADIUS > gridH - groundH;
        }),
        cactusBalls.end());
}

bool MainWindow::pipeRowSolid(const Pipe &p, int row) const
{
    // A row is passable inside the original gap or inside any blade-cut hole.
    if (row >= p.gapTop && row < p.gapTop + p.gapHeight) return false;
    for (const auto &g : p.extraGaps) {
        if (row >= g.first && row < g.second) return false;
    }
    return true;
}

void MainWindow::cutPipeAt(Pipe &p, float y)
{
    int playableH = gridH - groundH;
    int h = qMin(cfg.bladeGap, playableH);
    int top = qBound(0, (int)std::floor(y) - h / 2, playableH - h);
    p.extraGaps.push_back({top, top + h});

    // A blade-cut trunk never grows a plant eater: remove any that is pending
    // or already out, and make sure it cannot trigger later.
    p.hasEater = false;
    p.eaterTriggered = false;
    p.eaterOut = 0;
}

void MainWindow::updateBlades(Bird &owner)
{
    std::vector<FeatherBlade> &blades = owner.blades;
    if (blades.empty()) return;

    const int playableH = gridH - groundH;
    std::vector<FeatherBlade> alive;

    for (auto &b : blades) {
        const float oldFront = b.x;
        const float newFront = b.x + cfg.bladeSpeed;
        const int row = (int)std::floor(b.y);

        // Find the first thing the blade's tip reaches this tick.
        enum Kind { None, Ball, Cannon, Log } kind = None;
        float bestX = 1e9f;
        int ballIdx = -1;
        int pipeIdx = -1;

        if (hardMode) {
            for (int i = 0; i < (int)cactusBalls.size(); i++) {
                const auto &ball = cactusBalls[i];
                float x0 = ball.x - CACTUS_BALL_RADIUS;
                float x1 = ball.x + CACTUS_BALL_RADIUS;
                if (std::fabs(ball.y - b.y) > CACTUS_BALL_RADIUS) continue;
                if (x1 < oldFront || x0 > newFront) continue;
                float hx = qMax(x0, oldFront);
                if (hx < bestX) { bestX = hx; kind = Ball; ballIdx = i; }
            }
            if (cannonAlive) {
                float x0 = cannonX, x1 = cannonX + CANNON_W;
                if (b.y >= cannonY && b.y <= cannonY + CANNON_H &&
                    x1 >= oldFront && x0 <= newFront) {
                    float hx = qMax(x0, oldFront);
                    if (hx < bestX) { bestX = hx; kind = Cannon; }
                }
            }
        }

        for (int i = 0; i < (int)pipes.size(); i++) {
            const auto &p = pipes[i];
            bool solidPipe = !trollMode || !p.fakePipe || p.fakeActivated;
            if (!solidPipe) continue;
            float x0 = p.x, x1 = p.x + pipeWidth;
            if (x1 < oldFront || x0 > newFront) continue;
            if (!pipeRowSolid(p, row)) continue;
            float hx = qMax(x0, oldFront);
            if (hx < bestX) { bestX = hx; kind = Log; pipeIdx = i; }
        }

        if (kind == Ball) {
            cactusBalls.erase(cactusBalls.begin() + ballIdx);
            continue; // blade consumed
        }
        if (kind == Cannon) {
            if (++cannonHits >= cfg.cannonHitsToKill)
                cannonAlive = false;
            continue;
        }
        if (kind == Log) {
            cutPipeAt(pipes[pipeIdx], b.y);
            continue;
        }

        if (row < 0 || row >= playableH) continue; // left the playfield
        b.x = newFront;
        if (b.x - cfg.bladeLength > gridW) continue; // flew off screen
        alive.push_back(b);
    }

    blades.swap(alive);
}

void MainWindow::updateTroll()
{
    if (!trollMode) return;

    // Size effect timers (every bird shares the same troll effect).
    for (auto &b : birds) {
        if (b.scaleMs > 0) {
            b.scaleMs -= TICK_MS;
            if (b.scaleMs <= 0) {
                b.scaleMs = 0;
                b.scale = 1;
            }
        }
    }

    if (windX != 0.0f) {
        windAnimMs += TICK_MS;
        // Gradually fade the horizontal force after a wind burst.
        windX *= 0.88f;
        if (std::fabs(windX) < 0.08f) {
            windX = 0.0f;
            windAnimMs = 0;
        }
    }

    if (fakeGapMs > 0) {
        fakeGapMs -= TICK_MS;
        if (fakeGapMs <= 0) {
            fakeGapMs = 0;
            // Restore the gap when its temporary displacement ends.
            for (auto &p : pipes) {
                if (!p.gapShifted) continue;
                p.gapTop = p.originalGapTop;
                p.gapShifted = false;
            }
        }
    }

    trollEventMs -= TICK_MS;
    if (trollEventMs > 0) return;
    trollEventMs = QRandomGenerator::global()->bounded(cfg.trollEventMinMs, cfg.trollEventMaxMs);

    int event = QRandomGenerator::global()->bounded(4);
    const int refX = homeX;   // the birds' starting column

    // Make a nearby fake pipe real at the last moment.
    for (auto &p : pipes) {
        if (p.fakePipe && !p.fakeActivated && p.x > refX + 5 && p.x < refX + 35) {
            p.fakeActivated = true;
            break;
        }
    }

    if (event == 0) {
        // Sudden wind burst.
        if (cfg.trollWind) {
            windX = QRandomGenerator::global()->bounded(2) ? cfg.windStrength : -cfg.windStrength;
            windAnimMs = 0;
        }
    } else if (event == 1) {
        // Fake gap: the visible opening suddenly shifts/closes briefly.
        if (cfg.trollFakeGap && !pipes.empty()) {
            int idx = QRandomGenerator::global()->bounded((int)pipes.size());
            Pipe &p = pipes[idx];
            if (p.x > refX - 10 && p.x < refX + 45) {
                int shift = QRandomGenerator::global()->bounded(2) ? cfg.fakeGapShiftCells : -cfg.fakeGapShiftCells;
                fakeGapMs = cfg.fakeGapDurationMs;
                p.originalGapTop = p.gapTop;
                p.gapShifted = true;
                p.gapTop += shift;
                int playableH = gridH - groundH;
                p.gapTop = qBound(2, p.gapTop, qMax(2, playableH - p.gapHeight - 2));
            }
        }
    } else if (event == 2) {
        // Sudden horizontal pipe lunge.
        if (cfg.trollPipeLunge) {
            for (auto &p : pipes) {
                if (p.windAffected && p.x > refX && p.x < refX + 50) {
                    p.x -= QRandomGenerator::global()->bounded(cfg.pipeLungeMinCells, cfg.pipeLungeMaxCells);
                    break;
                }
            }
        }
    } else if (cfg.trollBirdResize) {
        // Randomly make the bird huge or tiny for a short stretch.
        int scale = QRandomGenerator::global()->bounded(2) ? 2 : 1;
        if (scale == 1 && QRandomGenerator::global()->bounded(3) == 0)
            scale = 3;
        const int ms = QRandomGenerator::global()->bounded(cfg.resizeMinMs, cfg.resizeMaxMs);
        for (auto &b : birds) {
            b.scale = scale;
            b.scaleMs = ms;
        }
    }

    // Very occasional fake GAME OVER prank.
    if (cfg.trollFakeGameOver &&
        QRandomGenerator::global()->bounded(100) < cfg.fakeGameOverChancePct && score >= cfg.fakeGameOverMinScore) {
        trollPauseMs = QRandomGenerator::global()->bounded(900, 1700);
        ui->statusLabel->setText("GAME OVER...\nSPACE to retry");
    }
}

int MainWindow::headingSlotPixelX(int slot, int fontSize) const
{
    QFont font("Arial", fontSize, QFont::Bold);
    QFontMetrics fm(font);
    int total = fm.horizontalAdvance(headingText);
    int imgW = gridW * CELL;
    int left = (imgW/5 - total/2);
    return left + fm.horizontalAdvance(headingText.left(slot));
}

void MainWindow::tryDropHeadingLetter()
{
    if (headingRefillSlot >= 0) return;
    for (const auto &L : fallingLetters) {
        if (!L.grounded) return;
    }

    std::vector<int> candidates;
    for (int i = 0; i < headingText.size(); i++) {
        if (headingText[i] == QLatin1Char(' ')) continue;
        if (headingInPlace[i])
            candidates.push_back(i);
    }
    if (candidates.empty()) return;

    int slot = candidates[QRandomGenerator::global()->bounded((int)candidates.size())];
    headingInPlace[slot] = 0;

    int fontSize = 22;
    FallingLetter L;
    L.ch = headingText[slot];
    L.slotIndex = slot;
    L.x = headingSlotPixelX(slot, fontSize) / (float)CELL;
    L.y = 2.0f;
    L.velY = 0.2f;
    L.grounded = false;
    fallingLetters.push_back(L);

    headingRefillWait = 10;
    headingRefillSlot = slot;
    headingRefillT = 0.0f;
}

void MainWindow::updateHeadingLetters()
{
    int playableH = gridH - groundH;

    if (!cfg.trollFallingLetters) {
        fallingLetters.clear();
        return;
    }

    if (headingRefillWait > 0) {
        headingRefillWait--;
    } else if (headingRefillSlot >= 0) {
        headingRefillT += 0.08f;
        if (headingRefillT >= 1.0f) {
            headingRefillT = 1.0f;
            headingInPlace[headingRefillSlot] = 1;
            headingRefillSlot = -1;
            headingDropDelay = QRandomGenerator::global()->bounded(70, 160);
        }
    } else {
        headingDropDelay--;
        if (headingDropDelay <= 0)
            tryDropHeadingLetter();
    }

    for (auto &L : fallingLetters) {
        if (!L.grounded) {
            L.x -= worldSpeed()*0.5f;
            L.velY += cfg.gravity * 0.7f;
            if (L.velY > cfg.maxFallSpeed) L.velY = cfg.maxFallSpeed;
            L.y += L.velY;
            if (L.y + LETTER_H >= playableH) {
                L.y = (float)(playableH - LETTER_H);
                L.velY = 0;
                L.grounded = true;
            }
        } else {
            L.x -= worldSpeed();
        }
    }

    fallingLetters.erase(
        std::remove_if(fallingLetters.begin(), fallingLetters.end(),
                       [](const FallingLetter &L) { return L.x + LETTER_W < 0; }),
        fallingLetters.end());
}

bool MainWindow::letterHitsBird(const FallingLetter &L, const Bird &b) const
{
    int by = (int)b.y;
    int bx = birdCol(b);
    int lx = (int)L.x;
    int ly = (int)L.y;
    return bx + birdW(b) > lx && bx < lx + LETTER_W &&
           by + birdH(b) > ly && by < ly + LETTER_H;
}

bool MainWindow::eaterHitsBird(const Pipe &p, const Bird &b) const
{
    if (!p.hasEater || p.eaterOut < 1.0f) return false;
    int by = (int)b.y;
    int bx = birdCol(b);
    int px = (int)p.x;
    int headW = qMax(3, pipeWidth - 2);
    int hx = px + (pipeWidth - headW) / 2;
    int hy, hh;
    hh = (int)p.eaterOut;
    if (hh < 1) return false;
    if (p.eaterFromTop) {
        hy = p.gapTop;
    } else {
        hy = p.gapTop + p.gapHeight - hh;
    }
    return bx + birdW(b) > hx && bx < hx + headW &&
           by + birdH(b) > hy && by < hy + hh;
}

bool MainWindow::checkCollision(const Bird &b)
{
    int by = (int)b.y;
    int bx = birdCol(b);
    int bw = birdW(b);
    int bh = birdH(b);
    int playableH = gridH - groundH;

    if (by + bh > playableH) return true;
    if (by < 0) return true;

    for (const auto &p : pipes) {
        float px = p.x;
        bool solidPipe = !trollMode || !p.fakePipe || p.fakeActivated;
        if (solidPipe && bx + bw > px && bx < px + pipeWidth) {
            for (int r = by; r < by + bh; r++) {
                if (pipeRowSolid(p, r))
                    return true;
            }
        }
        if (hardMode && eaterHitsBird(p, b)) return true;
    }

    if (hardMode) {
        for (const auto &ball : cactusBalls) {
            if (ball.x + CACTUS_BALL_RADIUS > bx &&
                ball.x - CACTUS_BALL_RADIUS < bx + bw &&
                ball.y + CACTUS_BALL_RADIUS > by &&
                ball.y - CACTUS_BALL_RADIUS < by + bh)
                return true;
        }
    }

    if (trollMode) {
        for (const auto &L : fallingLetters) {
            if (letterHitsBird(L, b)) return true;
        }
    }
    return false;
}

void MainWindow::fillCell(QImage &img, int cx, int cy, int r, int g, int b)
{
    if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return;
    int px = cx * CELL;
    int py = cy * CELL;
    QRgb color = qRgb(r, g, b);
    for (int dy = 0; dy < CELL && (py + dy) < img.height(); dy++) {
        for (int dx = 0; dx < CELL && (px + dx) < img.width(); dx++) {
            img.setPixel(px + dx, py + dy, color);
        }
    }
}

void MainWindow::drawForestBg(QImage &img)
{
    const int W = img.width();
    const int H = img.height();
    const int playPx = (gridH - groundH) * CELL;

    // Static sky is rendered once and reused every frame.
    if (bgCache.size() != img.size()) {
        QImage c(W, H, QImage::Format_RGB32);
        QPainter p(&c);
        p.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient sky(0, 0, 0, playPx);
        sky.setColorAt(0.00, QColor(3, 6, 10));
        sky.setColorAt(0.45, QColor(7, 16, 20));
        sky.setColorAt(0.80, QColor(14, 30, 31));
        sky.setColorAt(1.00, QColor(22, 40, 38));
        p.fillRect(0, 0, W, H, sky);

        // A dim, ominous crimson glow low on the horizon: not sunlight,
        // more a sign that something is watching from the dark.
        QRadialGradient glow(W * 0.70, playPx * 0.92, W * 0.65);
        glow.setColorAt(0.0, QColor(110, 28, 34, 85));
        glow.setColorAt(0.5, QColor(80, 22, 28, 35));
        glow.setColorAt(1.0, QColor(80, 22, 28, 0));
        p.fillRect(0, 0, W, playPx, glow);
        p.end();
        bgCache = c;
    }
    QPainter p(&img);
    p.drawImage(0, 0, bgCache);
}

void MainWindow::drawTrees(QImage &img)
{
    // Three parallax layers (rolling hills, mid forest, near forest). All of
    // them are functions of one never-wrapped scroll value, so the backdrop
    // is continuous and never visibly repeats.
    const int W = img.width();
    const int playPx = (gridH - groundH) * CELL;
    const double scroll = bgScroll * CELL;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setClipRect(0, 0, W, playPx);
    p.setPen(Qt::NoPen);

    auto hashU = [](int i, int salt) -> unsigned {
        unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
        x = (x ^ (x >> 13)) * 1274126177u;
        return x ^ (x >> 16);
    };
    auto frac = [&](int i, int salt) { return (hashU(i, salt) & 0xFFFF) / 65535.0f; };

    auto fog = [&](float y0, float y1, int alpha) {
        QLinearGradient g(0, y0, 0, y1);
        g.setColorAt(0.0, QColor(34, 66, 62, 0));
        g.setColorAt(1.0, QColor(34, 66, 62, alpha));
        p.fillRect(QRectF(0, y0, W, y1 - y0), g);
    };

    // ---- Layer 0: distant rolling hills (continuous sine sum) ----
    {
        const double s0 = scroll * 0.06;
        QPolygonF hill;
        hill << QPointF(-10, playPx + 2);
        for (int x = -10; x <= W + 10; x += 6) {
            double wx = x + s0;
            double y = playPx * (0.60 - 0.050 * std::sin(wx * 0.0041 + 1.0)
                                      - 0.035 * std::sin(wx * 0.0093 + 2.0)
                                      - 0.018 * std::sin(wx * 0.0210 + 0.5));
            hill << QPointF(x, y);
        }
        hill << QPointF(W + 10, playPx + 2);
        QLinearGradient hg(0, playPx * 0.45, 0, playPx);
        hg.setColorAt(0.0, QColor(19, 38, 38));
        hg.setColorAt(1.0, QColor(12, 26, 27));
        p.setBrush(hg);
        p.drawPolygon(hill);
        fog(playPx * 0.5f, (float)playPx, 90);
    }

    // A smooth, stylised spruce: soft drooping tiers, no hard pixel edges.
    auto spruce = [&](float cx, float baseY, float h, float w, const QColor &col) {
        const int T = 4;
        const float top = baseY - h;
        float cxo[T], cyo[T], exo[T], eyo[T], nxo[T], nyo[T];
        float prevY = top;
        for (int i = 0; i < T; i++) {
            float t = (i + 1) / (float)T;
            float yb = top + h * (0.18f + 0.82f * t);
            float hw = w * 0.5f * (0.30f + 0.70f * t);
            cxo[i] = hw * 0.78f;  cyo[i] = prevY + (yb - prevY) * 0.35f;
            exo[i] = hw;          eyo[i] = yb;
            nxo[i] = hw * 0.42f;  nyo[i] = yb - h * 0.025f;
            prevY = nyo[i];
        }
        QPainterPath path;
        path.moveTo(cx, top);
        for (int i = 0; i < T; i++) {
            path.quadTo(cx + cxo[i], cyo[i], cx + exo[i], eyo[i]);
            if (i < T - 1) path.lineTo(cx + nxo[i], nyo[i]);
        }
        path.lineTo(cx - exo[T - 1], eyo[T - 1]);
        for (int i = T - 1; i >= 1; i--) {
            path.quadTo(cx - cxo[i], cyo[i], cx - nxo[i - 1], nyo[i - 1]);
            path.lineTo(cx - exo[i - 1], eyo[i - 1]);
        }
        path.quadTo(cx - cxo[0], cyo[0], cx, top);
        path.closeSubpath();
        p.setBrush(col);
        p.drawPath(path);
    };

    struct Layer { double factor; float slotW; float hMin, hMax; float widthRatio; float skip; QColor col; int salt; float fogA; };
    const Layer layers[3] = {
        { 0.20, 30.0f,  0.28f, 0.46f, 0.34f, 0.08f, QColor(13, 29, 29), 11, 70 },
        { 0.42, 56.0f,  0.40f, 0.66f, 0.34f, 0.18f, QColor(8, 20, 21),  23, 90 },
        { 0.80, 150.0f, 0.62f, 0.92f, 0.30f, 0.40f, QColor(4, 11, 12),  37, 0 }
    };

    for (int li = 0; li < 3; li++) {
        const Layer &L = layers[li];
        const double s = scroll * L.factor;
        const int i0 = (int)std::floor(s / L.slotW) - 2;
        const int i1 = (int)std::floor((s + W) / L.slotW) + 3;
        for (int i = i0; i <= i1; i++) {
            if (frac(i, L.salt) < L.skip) continue;              // natural gaps
            float h = playPx * (L.hMin + (L.hMax - L.hMin) * frac(i, L.salt + 1));
            float w = h * L.widthRatio * (0.85f + 0.3f * frac(i, L.salt + 2));
            float wx = i * L.slotW + L.slotW * 0.8f * (frac(i, L.salt + 3) - 0.5f);
            float cx = (float)(wx - s);
            if (cx < -w || cx > W + w) continue;
            spruce(cx, (float)playPx + 2.0f, h, w, L.col);
        }
        if (L.fogA > 0)
            fog(playPx * 0.55f, (float)playPx, (int)L.fogA);
    }

    // Slow drifting mist wisps.
    {
        const double ft = scroll * 0.05;
        for (int i = 0; i < 3; i++) {
            float r = W * (0.35f + 0.1f * i);
            double span = W + 2.0 * r;
            float x = (float)(std::fmod(i * span / 3.0 - ft * (1.0 + 0.4 * i), span));
            if (x < 0) x += (float)span;
            x -= r;
            float y = playPx * (0.72f + 0.08f * i);
            p.save();
            p.translate(x, y);
            p.scale(1.0, 0.18);
            QRadialGradient g(QPointF(0, 0), r);
            g.setColorAt(0.0, QColor(60, 96, 90, 38));
            g.setColorAt(1.0, QColor(60, 96, 90, 0));
            p.setBrush(g);
            p.drawEllipse(QPointF(0, 0), r, r);
            p.restore();
        }
    }
}

namespace {
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
float clamp01(float t) { return t < 0 ? 0.0f : (t > 1 ? 1.0f : t); }
float smoothf(float t) { t = clamp01(t); return t * t * (3.0f - 2.0f * t); }

enum WingMode { WM_OVER, WM_SHOOT, WM_BRAKE, WM_THRUST, WM_FLAP, WM_TIRED, WM_IDLE, WM_CRUISE };

// Wing direction angle in degrees: 0 = straight back, 90 = straight up,
// 180 = straight forward, 270 / -90 = straight down.
float wingAngleFor(WingMode m, float t, float phase, float vel)
{
    switch (m) {
    case WM_OVER:   return -75.0f;
    case WM_SHOOT: {
        // wind-up -> fling forward -> recover
        t = clamp01(t);
        if (t < 0.30f) return lerpf(60.0f, 110.0f, smoothf(t / 0.30f));
        if (t < 0.60f) return lerpf(110.0f, 185.0f, smoothf((t - 0.30f) / 0.30f));
        return lerpf(185.0f, 85.0f, smoothf((t - 0.60f) / 0.40f));
    }
    case WM_BRAKE:  return 162.0f + 52.0f * std::sin(phase);   // beats FORWARD (reverse flap)
    case WM_THRUST: return 25.0f + 70.0f * std::sin(phase);    // fast, swept-back beats
    case WM_FLAP:
        t = clamp01(t);
        if (t < 0.55f) return lerpf(100.0f, -55.0f, smoothf(t / 0.55f));   // power downstroke
        return lerpf(-55.0f, 25.0f, smoothf((t - 0.55f) / 0.45f));         // slow recovery
    case WM_CRUISE: return 20.0f + 60.0f * std::sin(phase);     // smooth looping beat (menu)
    case WM_TIRED:  return -65.0f + 6.0f * std::sin(phase * 0.8f);
    default: {
        float v = vel < 0 ? 0.0f : (vel > 6.0f ? 6.0f : vel);
        return 25.0f + 10.0f * std::sin(phase) + v * 5.0f;
    }
    }
}

// A wing with scalloped primary feathers. Local +X runs along the span from
// the shoulder; the trailing edge is on local -Y.
void drawWingShape(QPainter &p, const QPointF &shoulder, float aDeg,
                   const QColor &fill, const QColor &edge)
{
    const float L = 16.0f;
    QPainterPath path;
    path.moveTo(0, 2.2);
    path.cubicTo(L * 0.35, 3.8, L * 0.8, 3.2, L + 2.0, 0.2);
    path.lineTo(L * 0.93, -3.5);
    path.lineTo(L * 0.84, -1.8);
    path.lineTo(L * 0.78, -6.2);
    path.lineTo(L * 0.68, -3.8);
    path.lineTo(L * 0.58, -7.8);
    path.lineTo(L * 0.46, -5.0);
    path.lineTo(L * 0.32, -7.4);
    path.lineTo(L * 0.15, -5.0);
    path.lineTo(0, -4.2);
    path.closeSubpath();

    p.save();
    p.translate(shoulder);
    p.rotate(aDeg + 180.0f);
    p.setPen(QPen(edge, 0.8));
    p.setBrush(fill);
    p.drawPath(path);
    QColor line = edge;
    line.setAlpha(edge.alpha() * 6 / 10);
    p.setPen(QPen(line, 0.6));
    p.drawLine(QPointF(L * 0.84, -1.8), QPointF(L * 0.45, 0.2));
    p.drawLine(QPointF(L * 0.68, -3.8), QPointF(L * 0.30, -0.4));
    p.drawLine(QPointF(L * 0.46, -5.0), QPointF(L * 0.15, -1.2));
    p.restore();
}

void drawMiniFeather(QPainter &p, float x, float y, float len, int alpha)
{
    QPolygonF f;
    f << QPointF(x, y) << QPointF(x - len * 0.7f, y - 2.5f)
      << QPointF(x - len, y) << QPointF(x - len * 0.7f, y + 2.5f);
    p.setPen(QPen(QColor(120, 200, 255, alpha), 0.8));
    p.setBrush(QColor(235, 248, 255, alpha));
    p.drawPolygon(f);
}
} // namespace

namespace {
struct BirdPose {
    float cx = 0, cy = 0, k = 1;
    WingMode mode = WM_IDLE;
    float t = 0, shootT = 0, phase = 0, vel = 0, tilt = 0, speedMul = 1;
    bool thrust = false, brake = false, tired = false, over = false, shooting = false;
    int hue = 0;           // colour shift in degrees (players 2-4 get other colours)
    int alpha = 255;       // < 255 while a bird is protected after respawning
};

void paintBird(QPainter &p, const BirdPose &s)
{
    const float k = s.k, cx = s.cx, cy = s.cy;
    const float S = 0.8f * k;                // art units -> pixels
    // Rotate the hue of a palette colour (used to tell the players apart).
    auto T = [&](const QColor &col) {
        if (s.hue == 0) return col;
        int h, sa, v, al;
        col.getHsv(&h, &sa, &v, &al);
        return QColor::fromHsv((h + s.hue) % 360, sa, v, al);
    };
    const bool thrustActive = s.thrust, brakeActive = s.brake, tired = s.tired, over = s.over;
    const float wingPhase = s.phase, birdVel = s.vel, birdTilt = s.tilt, speedMul = s.speedMul;
    const float tShoot = s.shootT;
    const WingMode mode = s.mode;
    const float t = (mode == WM_SHOOT) ? s.shootT : s.t;
    const float a      = wingAngleFor(mode, t, wingPhase, birdVel);
    const float ghostA = wingAngleFor(mode, t - 0.12f, wingPhase - 0.7f, birdVel);
    const float farA   = a + (mode == WM_BRAKE ? 18.0f : -18.0f);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(s.alpha / 255.0);
    p.translate(cx, cy);

    // ---- Effects drawn in the world frame (not tilted) ----
    if (speedMul > 1.08f) {
        // Speed streaks behind the bird while accelerating.
        float len = (speedMul - 1.0f) * 40.0f * k;
        p.setPen(QPen(QColor(255, 255, 255, 130), 1.5));
        const float ys[4] = { -14.0f, -5.0f, 5.0f, 14.0f };
        for (int i = 0; i < 4; i++) {
            float jitter = 3.0f * std::sin(wingPhase * 2.0f + i * 1.7f) * k;
            p.drawLine(QPointF(-16 * k - len - jitter, ys[i] * k),
                       QPointF(-14 * k - jitter, ys[i] * k));
        }
    }
    if (brakeActive) {
        // Air being pushed forward by the reversed wing beats.
        for (int i = 0; i < 3; i++) {
            p.setPen(QPen(QColor(255, 255, 255, 170 - i * 50), 1.5));
            p.drawArc(QRectF((20 + i * 6) * k, -11 * k, 8 * k, 22 * k), -60 * 16, 120 * 16);
        }
    }
    if (s.shooting) {
        // Feathers flung off the wing as it snaps forward.
        float f = clamp01(tShoot);
        int alpha = (int)(220 * (1.0f - f));
        for (int i = 0; i < 3; i++) {
            drawMiniFeather(p, (10 + f * 30 + i * 5) * k, (-4 + (i - 1) * 7 * f) * k,
                            8.0f * k, alpha);
        }
    }

    // ---- The bird itself, pitched and scaled ----
    p.save();
    p.rotate(birdTilt);
    p.scale(S, S);

    const QColor edge = T(QColor(170, 115, 0));
    const QColor bodyTop = T(tired ? QColor(235, 215, 110) : QColor(255, 214, 0));
    const QColor bodyMid = T(tired ? QColor(240, 225, 140) : QColor(255, 224, 40));
    const QColor bellyC = T(QColor(255, 242, 160));

    // Tail feathers fan out (wide when braking, tight when thrusting).
    {
        float spread = brakeActive ? 36.0f : (thrustActive ? 8.0f : 16.0f);
        float pitch  = brakeActive ? -8.0f : (thrustActive ? 4.0f : 0.0f);
        for (int i = -1; i <= 1; i++) {
            p.save();
            p.translate(-9.5, 1.0);
            p.rotate(180.0f + i * spread + pitch);
            p.setPen(QPen(T(QColor(190, 110, 10)), 0.8));
            p.setBrush(T(QColor(235, 150, 20)));
            p.drawRoundedRect(QRectF(0, -1.7, 12, 3.4), 1.6, 1.6);
            p.restore();
        }
    }

    // Far wing (darker, behind the body).
    drawWingShape(p, QPointF(-0.5, -3.0), farA, T(QColor(205, 135, 0)), T(QColor(140, 90, 0)));

    // Body.
    {
        QLinearGradient g(0, -8.5, 0, 8.5);
        g.setColorAt(0.0, bodyTop);
        g.setColorAt(0.5, bodyMid);
        g.setColorAt(1.0, bellyC);
        p.setPen(QPen(edge, 0.9));
        p.setBrush(g);
        p.drawEllipse(QPointF(0, 0), 11.0, 8.5);
    }

    // Head, crest, cheek.
    p.setPen(QPen(edge, 0.9));
    p.setBrush(bodyMid);
    p.drawEllipse(QPointF(7.5, -5.5), 6.0, 6.0);
    {
        QPolygonF crest;
        crest << QPointF(4.8, -10.2) << QPointF(3.2, -14.2) << QPointF(6.4, -11.3)
              << QPointF(7.2, -15.0) << QPointF(8.6, -11.0);
        p.setBrush(T(QColor(255, 190, 0)));
        p.drawPolygon(crest);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 150, 120, 150));
    p.drawEllipse(QPointF(9.8, -3.2), 1.9, 1.5);

    // Eye.
    if (over) {
        p.setPen(QPen(QColor(40, 20, 0), 1.1));
        p.drawLine(QPointF(7.8, -8.2), QPointF(10.8, -5.4));
        p.drawLine(QPointF(10.8, -8.2), QPointF(7.8, -5.4));
    } else {
        p.setPen(QPen(QColor(60, 40, 0), 0.6));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(9.3, -6.8), 2.8, 2.8);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 10, 0));
        p.drawEllipse(QPointF(10.1, -6.7), 1.4, 1.4);
        p.setPen(QPen(QColor(60, 30, 0), 1.2));
        if (thrustActive)       // determined brow
            p.drawLine(QPointF(6.6, -10.4), QPointF(12.2, -8.4));
        else if (tired)         // heavy eyelid
            p.drawLine(QPointF(6.4, -6.9), QPointF(12.2, -6.9));
    }

    // Beak (opens when braking or exhausted).
    {
        float gap = (brakeActive || tired) ? 1.6f : 0.0f;
        p.setPen(QPen(QColor(200, 100, 0), 0.7));
        p.setBrush(QColor(255, 150, 0));
        QPolygonF upper;
        upper << QPointF(11.8, -4.4) << QPointF(18.0, -2.6 - gap * 0.3) << QPointF(11.8, -1.6);
        p.drawPolygon(upper);
        QPolygonF lower;
        lower << QPointF(11.8, -1.2) << QPointF(16.0, -0.2 + gap) << QPointF(11.4, 1.2 + gap * 0.4);
        p.setBrush(QColor(240, 130, 0));
        p.drawPolygon(lower);
    }

    // Near wing with a faint motion ghost behind it.
    if (std::fabs(a - ghostA) > 8.0f && !over)
        drawWingShape(p, QPointF(-1.0, -2.0), ghostA, T(QColor(255, 190, 40, 70)), T(QColor(170, 110, 0, 70)));
    drawWingShape(p, QPointF(-1.0, -2.0), a, T(QColor(255, 178, 0)), edge);

    // Sweat drop when exhausted.
    if (tired) {
        p.setPen(QPen(QColor(90, 150, 220), 0.6));
        p.setBrush(QColor(140, 200, 255));
        p.drawEllipse(QPointF(2.0, -14.5 + 1.5 * std::sin(wingPhase * 2.0f)), 1.6, 2.3);
    }

    p.restore();   // tilted/scaled bird frame
    p.restore();   // outer frame (translate to bird centre)
}
} // namespace

void MainWindow::drawBird(QImage &img, const Bird &b)
{
    const int bw = birdW(b);
    const int bh = birdH(b);
    const int by = (int)b.y;

    BirdPose s;
    s.k = b.scale * cfg.birdSize / 4.0f;     // the art is drawn for a 4-cell bird
    s.cx = (birdCol(b) + bw / 2.0f) * CELL;
    s.cy = (by + bh / 2.0f) * CELL;
    s.over = !b.alive || (state == GameOver && !multiplayer);
    s.tired = !s.over && !canAct(b);
    s.thrust = b.thrustActive;
    s.brake = b.brakeActive;
    s.shooting = b.shootAnimMs > 0;
    s.phase = b.wingPhase;
    s.vel = b.vel;
    s.tilt = b.tilt;
    s.speedMul = b.speedMul;
    s.t = 1.0f - b.flapAnimMs / (float)FLAP_ANIM_MS;
    s.shootT = 1.0f - b.shootAnimMs / (float)SHOOT_ANIM_MS;
    s.hue = b.hue;
    // Blink while protected after a respawn.
    if (b.invulnMs > 0 && b.alive)
        s.alpha = ((b.invulnMs / 120) % 2 == 0) ? 110 : 220;

    if (s.over) s.mode = WM_OVER;
    else if (b.shootAnimMs > 0) s.mode = WM_SHOOT;
    else if (b.brakeActive) s.mode = WM_BRAKE;
    else if (b.thrustActive) s.mode = WM_THRUST;
    else if (b.flapAnimMs > 0) s.mode = WM_FLAP;
    else if (s.tired) s.mode = WM_TIRED;
    else s.mode = WM_IDLE;

    QPainter p(&img);
    paintBird(p, s);
}

void MainWindow::drawBirdNames(QImage &img)
{
    // Small "P1 / P2..." tags above the birds in multiplayer.
    static const QColor tagColors[my_label::MAX_PLAYERS] = {
        QColor(255, 214, 0), QColor(90, 190, 255), QColor(255, 90, 140), QColor(120, 230, 90) };
    for (const auto &b : birds) {
        if (!b.alive) continue;
        const int cx = (int)((birdCol(b) + birdW(b) / 2.0f) * CELL);
        const int top = (int)(b.y * CELL) - 12 - (b.scale > 1 ? 6 : 0);
        const QColor &col = tagColors[b.id % my_label::MAX_PLAYERS];
        drawText(img, QString("P%1").arg(b.id + 1), cx, top, col.red(), col.green(), col.blue(), 10);
    }
}

void MainWindow::drawPipes(QImage &img)
{
    // Pipes are drawn as tree trunks: dark bark with grooves, knots and moss,
    // and a ringed cut face where the trunk is open to the gap.
    const int playableH = gridH - groundH;
    QPainter pt(&img);
    pt.setRenderHint(QPainter::Antialiasing, true);

    auto hashU = [](int a, int b) -> unsigned {
        unsigned x = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
        x = (x ^ (x >> 13)) * 1274126177u;
        return x ^ (x >> 16);
    };
    auto frac = [&](int a, int b) { return (hashU(a, b) & 0xFFFF) / 65535.0f; };

    for (const auto &p : pipes) {
        const float x = p.x * (float)CELL;          // smooth, sub-pixel position
        const float w = pipeWidth * (float)CELL;
        if (x + w + CELL * 2 < 0 || x - CELL * 2 > img.width()) continue;

        // Palette: dark, slightly mossy bark. Troll decoys are paler dead wood.
        QColor edgeC(10, 7, 6), barkC(40, 29, 22), hiC(70, 53, 38);
        QColor faceC(104, 78, 52), ringC(66, 46, 30);
        if (trollMode && p.fakePipe && !p.fakeActivated) {
            edgeC = QColor(24, 22, 20); barkC = QColor(66, 62, 56); hiC = QColor(104, 98, 88);
            faceC = QColor(142, 132, 112); ringC = QColor(96, 88, 72);
        }

        int r = 0;
        while (r < playableH) {
            if (!pipeRowSolid(p, r)) { r++; continue; }
            int start = r;
            while (r < playableH && pipeRowSolid(p, r)) r++;
            int end = r;                                  // solid rows [start, end)
            const float y0 = start * (float)CELL;
            const float y1 = end * (float)CELL;

            // Soft shadow cast on the backdrop to the right of the trunk.
            {
                QLinearGradient sg(x + w, 0, x + w + 7, 0);
                sg.setColorAt(0.0, QColor(0, 0, 0, 70));
                sg.setColorAt(1.0, QColor(0, 0, 0, 0));
                pt.fillRect(QRectF(x + w, y0, 7, y1 - y0), sg);
            }

            pt.save();
            pt.setClipRect(QRectF(x, y0, w, y1 - y0));

            // Round trunk shading.
            QLinearGradient g(x, 0, x + w, 0);
            g.setColorAt(0.00, edgeC);
            g.setColorAt(0.16, barkC);
            g.setColorAt(0.38, hiC);
            g.setColorAt(0.58, barkC);
            g.setColorAt(0.88, edgeC.lighter(110));
            g.setColorAt(1.00, edgeC);
            pt.setPen(Qt::NoPen);
            pt.fillRect(QRectF(x, y0, w, y1 - y0), g);

            // Vertical bark grooves (wavy lines, continuous along the trunk).
            const int grooves = 7;
            for (int i = 0; i < grooves; i++) {
                float fx = (i + 0.5f + (frac(p.seed, 100 + i) - 0.5f) * 0.6f) / grooves;
                float ph = frac(p.seed, 200 + i) * 6.28f;
                float amp = 1.0f + 1.6f * frac(p.seed, 300 + i);
                QPainterPath gp;
                bool first = true;
                for (float yy = y0 - 4; yy <= y1 + 4; yy += 7.0f) {
                    float gx = x + w * fx + amp * std::sin(yy * 0.045f + ph);
                    if (first) { gp.moveTo(gx, yy); first = false; } else gp.lineTo(gx, yy);
                }
                pt.setBrush(Qt::NoBrush);
                pt.setPen(QPen(QColor(6, 4, 3, 120), 1.5));
                pt.drawPath(gp);
                pt.setPen(QPen(QColor(255, 235, 210, 18), 1.0));
                pt.translate(1.6, 0);
                pt.drawPath(gp);
                pt.translate(-1.6, 0);
            }

            // Knots, cracks and moss, placed per 70px slot so they stay put.
            const int slotCount = (int)(playableH * (float)CELL / 70.0f) + 2;
            for (int sl = 0; sl < slotCount; sl++) {
                float ky = sl * 70.0f + 8.0f + 54.0f * frac(p.seed, 400 + sl);
                if (ky < y0 - 20 || ky > y1 + 20) continue;
                float kx = x + w * (0.18f + 0.64f * frac(p.seed, 500 + sl));
                if (frac(p.seed, 600 + sl) < 0.55f) {            // knot
                    float rx = 3.0f + 3.5f * frac(p.seed, 700 + sl);
                    pt.setPen(QPen(QColor(8, 5, 4, 200), 1.2));
                    pt.setBrush(QColor(26, 17, 12));
                    pt.drawEllipse(QPointF(kx, ky), rx, rx * 1.5f);
                    pt.setPen(QPen(QColor(110, 80, 56, 120), 0.9));
                    pt.setBrush(Qt::NoBrush);
                    pt.drawEllipse(QPointF(kx, ky), rx * 0.55f, rx * 0.85f);
                } else {                                        // horizontal crack
                    pt.setPen(QPen(QColor(6, 4, 3, 170), 1.2));
                    pt.drawArc(QRectF(kx - 6, ky - 2, 12, 5), 200 * 16, 140 * 16);
                }
                if (frac(p.seed, 800 + sl) < 0.5f) {            // moss patch
                    float mx = x + w * (frac(p.seed, 900 + sl) < 0.5f ? 0.12f : 0.8f);
                    pt.setPen(Qt::NoPen);
                    pt.setBrush(QColor(34, 74, 44, 90));
                    pt.drawEllipse(QPointF(mx, ky + 18), 6.0f, 11.0f);
                }
            }

            // Extra roundness: darken both edges, keep the middle lit.
            {
                QLinearGradient sh(x, 0, x + w, 0);
                sh.setColorAt(0.00, QColor(0, 0, 0, 120));
                sh.setColorAt(0.22, QColor(0, 0, 0, 0));
                sh.setColorAt(0.70, QColor(0, 0, 0, 0));
                sh.setColorAt(1.00, QColor(0, 0, 0, 150));
                pt.setPen(Qt::NoPen);
                pt.fillRect(QRectF(x, y0, w, y1 - y0), sh);
            }
            pt.restore();

            // Outline.
            pt.setPen(QPen(QColor(4, 3, 2, 235), 1.3));
            pt.drawLine(QPointF(x + 0.6f, y0), QPointF(x + 0.6f, y1));
            pt.drawLine(QPointF(x + w - 0.6f, y0), QPointF(x + w - 0.6f, y1));

            // Cut faces with growth rings where the trunk is open to the gap.
            // The ellipse stays inside the solid rows so it matches collision.
            auto cutFace = [&](bool atTop) {
                float ry = qMin(w * 0.16f, (y1 - y0) / 2.0f);
                float cy = atTop ? y0 + ry : y1 - ry;
                QPointF c(x + w / 2.0f, cy);
                QRectF face(x, cy - ry, w, ry * 2);
                // soft shadow cast on the bark beside the cut
                QLinearGradient sg(0, atTop ? y0 + ry * 2 : y1 - ry * 2, 0, atTop ? y0 + ry * 2 + 9 : y1 - ry * 2 - 9);
                sg.setColorAt(0.0, QColor(0, 0, 0, 120));
                sg.setColorAt(1.0, QColor(0, 0, 0, 0));
                pt.fillRect(QRectF(x + 1, atTop ? y0 + ry * 2 : y1 - ry * 2 - 9, w - 2, 9), sg);

                pt.setPen(QPen(QColor(4, 3, 2, 235), 1.3));
                QRadialGradient fg(c, w * 0.5f);
                fg.setColorAt(0.0, faceC.lighter(115));
                fg.setColorAt(1.0, faceC.darker(125));
                pt.setBrush(fg);
                pt.drawEllipse(face);
                pt.setBrush(Qt::NoBrush);
                pt.setPen(QPen(ringC, 0.9));
                for (int k = 1; k <= 4; k++) {
                    float f = k / 5.0f;
                    pt.drawEllipse(c, w * 0.5f * f, ry * f);
                }
                pt.setPen(Qt::NoPen);
                pt.setBrush(ringC.darker(130));
                pt.drawEllipse(c, 1.5, 1.0);
            };
            if (start > 0)         cutFace(true);     // top end faces the gap
            if (end < playableH)   cutFace(false);    // bottom end faces the gap
        }
    }
}

void MainWindow::drawEaters(QImage &img)
{
    // A snapping flytrap / piranha plant growing out of the trunk: swaying
    // stem, a leaf, and a head whose two toothed jaws open and shut.
    QPainter pt(&img);
    pt.setRenderHint(QPainter::Antialiasing, true);
    const float PI = 3.14159265f;
    const float tms = (float)playMs;

    for (const auto &p : pipes) {
        if (!p.hasEater || p.eaterOut < 0.5f) continue;
        const int headWc = qMax(3, pipeWidth - 2);
        const float hxCells = p.x + (pipeWidth - headWc) / 2.0f;
        const int hh = (int)p.eaterOut;
        if (hh < 1) continue;
        const int hyCells = p.eaterFromTop ? p.gapTop : (p.gapTop + p.gapHeight - hh);
        const QRectF rc(hxCells * CELL, hyCells * (float)CELL, headWc * (float)CELL, hh * (float)CELL);

        const float W = (float)rc.width(), L = (float)rc.height(), hw = W / 2.0f;
        const float phase = p.seed * 0.01f;

        pt.save();
        if (p.eaterFromTop) {
            pt.translate(rc.center().x(), rc.top());          // grows downward
        } else {
            pt.translate(rc.center().x(), rc.bottom());       // grows upward
            pt.scale(1.0, -1.0);
        }
        // From here: origin = where the plant meets the trunk, +y = toward the bird.

        const float sway = 2.0f * std::sin(tms * 0.006f + phase);
        const float headH = qMin(L, W * 1.05f);
        const float yh = L - headH;                 // where the head starts
        const float hinge = yh + headH * 0.28f;     // jaw hinge line
        const float jawLen = L - hinge;
        const float jw = hw * 0.95f;

        // Dark mound where it bursts out of the wood.
        pt.setPen(Qt::NoPen);
        pt.setBrush(QColor(18, 30, 20));
        pt.drawEllipse(QPointF(0, 0), hw * 0.6f, 2.6);

        // Stem and a leaf.
        if (yh > 1.0f) {
            QPainterPath st;
            st.moveTo(0, 0);
            st.quadTo(sway * 2.0f, yh * 0.5f, sway, yh + 3.0f);
            pt.setBrush(Qt::NoBrush);
            pt.setPen(QPen(QColor(12, 52, 24), W * 0.34f, Qt::SolidLine, Qt::RoundCap));
            pt.drawPath(st);
            pt.setPen(QPen(QColor(36, 122, 54), W * 0.21f, Qt::SolidLine, Qt::RoundCap));
            pt.drawPath(st);
            pt.setPen(QPen(QColor(120, 200, 120, 70), 1.4));
            pt.drawPath(st);
            if (yh > 16.0f) {
                pt.save();
                pt.translate(sway * 0.9f, yh * 0.45f);
                pt.rotate(-55.0);
                QPainterPath leaf;
                leaf.moveTo(0, 0);
                leaf.quadTo(10, 7, 0, 16);
                leaf.quadTo(-4, 7, 0, 0);
                pt.setPen(QPen(QColor(8, 40, 18), 1.0));
                pt.setBrush(QColor(30, 108, 46));
                pt.drawPath(leaf);
                pt.restore();
            }
        }

        // ---- Head (swaying with the stem) ----
        pt.translate(sway, 0);

        // Back of the head (dome).
        {
            QLinearGradient dg(0, hinge - headH * 0.3f, 0, hinge + 2);
            dg.setColorAt(0.0, QColor(58, 160, 74));
            dg.setColorAt(1.0, QColor(24, 98, 44));
            pt.setPen(QPen(QColor(6, 34, 16), 1.2));
            pt.setBrush(dg);
            pt.drawEllipse(QPointF(0, hinge), hw * 0.95f, headH * 0.28f + 1.0f);
        }

        // Jaws open and shut.
        const float openDeg = 14.0f + 22.0f * (0.5f + 0.5f * std::sin(tms * 0.014f + phase));
        const float th = openDeg * PI / 180.0f;
        {   // dark red mouth interior between the jaws
            QPolygonF mouth;
            mouth << QPointF(0, hinge)
                  << QPointF(-std::sin(th) * jawLen, hinge + std::cos(th) * jawLen)
                  << QPointF( std::sin(th) * jawLen, hinge + std::cos(th) * jawLen);
            QLinearGradient mg(0, hinge, 0, L);
            mg.setColorAt(0.0, QColor(150, 28, 40));
            mg.setColorAt(1.0, QColor(60, 8, 16));
            pt.setPen(Qt::NoPen);
            pt.setBrush(mg);
            pt.drawPolygon(mouth);
        }
        for (int side = -1; side <= 1; side += 2) {      // -1 = left jaw, +1 = right jaw
            pt.save();
            pt.translate(0, hinge);
            pt.rotate(-side * openDeg);
            QPainterPath jaw;
            jaw.moveTo(0, 0);
            jaw.quadTo(side * jw * 2.0f, jawLen * 0.5f, 0, jawLen);
            jaw.quadTo(side * jw * 0.5f, jawLen * 0.5f, 0, 0);
            QLinearGradient jg(side * jw, 0, 0, 0);
            jg.setColorAt(0.0, QColor(14, 70, 32));
            jg.setColorAt(1.0, QColor(54, 150, 70));
            pt.setPen(QPen(QColor(6, 34, 16), 1.2));
            pt.setBrush(jg);
            pt.drawPath(jaw);

            // spots on the outside of the jaw
            pt.setPen(Qt::NoPen);
            pt.setBrush(QColor(170, 36, 48, 190));
            pt.drawEllipse(QPointF(side * jw * 0.52f, jawLen * 0.42f), 2.3, 2.3);
            pt.drawEllipse(QPointF(side * jw * 0.36f, jawLen * 0.72f), 1.7, 1.7);

            // teeth along the inner edge
            const int teeth = 4;
            pt.setBrush(QColor(240, 238, 226));
            pt.setPen(QPen(QColor(120, 110, 100), 0.5));
            for (int i = 0; i < teeth; i++) {
                float t = 0.16f + 0.78f * (i / (float)teeth);
                float ex = side * jw * t * (1.0f - t);          // inner edge x at this height
                float ty = jawLen * t;
                float tb = jawLen * 0.11f;
                QPolygonF tooth;
                tooth << QPointF(ex, ty) << QPointF(ex, ty + tb)
                      << QPointF(ex - side * hw * 0.30f, ty + tb * 0.5f);
                pt.drawPolygon(tooth);
            }
            pt.restore();
        }
        pt.restore();
    }
}

void MainWindow::drawCannon(QImage &img)
{
    // A floating barrel cactus with an angry face and a cactus-arm cannon
    // that swivels toward the bird and glows just before it fires.
    const float PI = 3.14159265f;
    float ax = cannonAimX, ay = cannonAimY;
    if (std::fabs(ax) + std::fabs(ay) < 0.01f) { ax = -1.0f; ay = 0.0f; }
    const float aimDeg = std::atan2(ay, ax) * 180.0f / PI;
    const float bob = 1.5f * std::sin((float)playMs * 0.006f);
    const float cx = (cannonX + CANNON_W / 2.0f) * CELL;
    const float cy = (cannonY + CANNON_H / 2.0f) * CELL + bob;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Soft ground-less shadow glow under the cactus.
    {
        QRadialGradient sg(cx, cy + 20, 26);
        sg.setColorAt(0.0, QColor(0, 0, 0, 70));
        sg.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sg);
        p.drawEllipse(QPointF(cx, cy + 20), 26, 8);
    }

    // Side arms (cactus style), behind the body.
    for (int side = -1; side <= 1; side += 2) {
        QPainterPath arm;
        arm.moveTo(cx + side * 10, cy + 5);
        arm.quadTo(cx + side * 22, cy + 5, cx + side * 21, cy - 6 - (side > 0 ? 3 : 0));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(10, 50, 22), 8.0, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(arm);
        p.setPen(QPen(QColor(48, 138, 62), 5.0, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(arm);
    }

    // Body.
    {
        QRadialGradient bg(cx - 5, cy - 7, 28);
        bg.setColorAt(0.0, QColor(124, 214, 98));
        bg.setColorAt(0.55, QColor(54, 146, 64));
        bg.setColorAt(1.0, QColor(20, 78, 34));
        p.setPen(QPen(QColor(8, 44, 18), 1.5));
        p.setBrush(bg);
        p.drawEllipse(QPointF(cx, cy), 15.5, 17.0);
    }
    // Ribs and spines.
    for (int k = -2; k <= 2; k++) {
        QPainterPath rib;
        rib.moveTo(cx + k * 2.6, cy - 16);
        rib.quadTo(cx + k * 15.0, cy, cx + k * 2.6, cy + 16);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(12, 60, 26, 140), 1.2));
        p.drawPath(rib);
        for (int yy = -10; yy <= 10; yy += 7) {
            float f = yy / 17.0f;
            float sx = cx + k * 7.5f * (1.0f - f * f);
            p.setPen(QPen(QColor(244, 236, 190, 190), 0.9));
            p.drawLine(QPointF(sx - 1.8, cy + yy - 1.8), QPointF(sx + 1.8, cy + yy + 1.8));
            p.drawLine(QPointF(sx - 1.8, cy + yy + 1.8), QPointF(sx + 1.8, cy + yy - 1.8));
        }
    }

    // Angry face; pupils look where the cannon aims.
    for (int side = -1; side <= 1; side += 2) {
        QPointF e(cx + side * 5.2, cy - 3.5);
        p.setPen(QPen(QColor(10, 30, 14), 0.8));
        p.setBrush(QColor(250, 244, 214));
        p.drawEllipse(e, 3.4, 3.1);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(170, 20, 28));
        p.drawEllipse(QPointF(e.x() + ax * 1.3, e.y() + ay * 1.3), 1.6, 1.6);
        p.setPen(QPen(QColor(10, 40, 16), 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(e.x() - side * 4.0, e.y() - 5.2), QPointF(e.x() + side * 0.6, e.y() - 3.2));
    }

    // Cannon barrel (a thick cactus arm) pointing at the bird.
    p.save();
    p.translate(cx, cy);
    p.rotate(aimDeg);
    {
        QLinearGradient bgd(0, -6.5, 0, 6.5);
        bgd.setColorAt(0.0, QColor(24, 90, 38));
        bgd.setColorAt(0.5, QColor(96, 190, 80));
        bgd.setColorAt(1.0, QColor(20, 76, 32));
        p.setPen(QPen(QColor(8, 44, 18), 1.4));
        p.setBrush(bgd);
        p.drawRoundedRect(QRectF(5, -6.5, 21, 13), 3.0, 3.0);
        p.setPen(QPen(QColor(244, 236, 190, 170), 0.9));
        for (int xx = 9; xx <= 21; xx += 6) {
            p.drawLine(QPointF(xx, -6.5), QPointF(xx + 1.5, -9.0));
            p.drawLine(QPointF(xx, 6.5), QPointF(xx + 1.5, 9.0));
        }
        // muzzle ring and dark opening
        p.setPen(QPen(QColor(6, 30, 12), 1.2));
        p.setBrush(QColor(44, 72, 36));
        p.drawRoundedRect(QRectF(23, -8.2, 6.5, 16.4), 2.5, 2.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(8, 6, 6));
        p.drawEllipse(QPointF(29.5, 0), 2.4, 5.6);
        // hub where the barrel joins the body
        p.setPen(QPen(QColor(8, 44, 18), 1.2));
        p.setBrush(QColor(40, 120, 54));
        p.drawEllipse(QPointF(5, 0), 7.5, 8.0);
        // charging glow just before it fires
        if (cannonFireMs < 400) {
            int a = (int)(200.0f * (1.0f - cannonFireMs / 400.0f));
            QRadialGradient gg(QPointF(30, 0), 13);
            gg.setColorAt(0.0, QColor(255, 150, 70, a));
            gg.setColorAt(1.0, QColor(255, 80, 40, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(gg);
            p.drawEllipse(QPointF(30, 0), 13, 13);
        }
    }
    p.restore();
}

void MainWindow::drawCactusBalls(QImage &img)
{
    // Round, spiky cactus balls that spin as they fly.
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    const float PI = 3.14159265f;
    for (const auto &ball : cactusBalls) {
        const QPointF c(ball.x * CELL, ball.y * CELL);
        const float rot = (ball.x + ball.y) * 0.35f;
        for (int i = 0; i < 12; i++) {
            float a = rot + i * (2.0f * PI / 12.0f);
            QPointF in(c.x() + std::cos(a) * 5.0f, c.y() + std::sin(a) * 5.0f);
            QPointF out(c.x() + std::cos(a) * 10.5f, c.y() + std::sin(a) * 10.5f);
            p.setPen(QPen(QColor(12, 56, 22), 2.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(in, out);
            p.setPen(QPen(QColor(236, 232, 176), 1.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(in, out);
        }
        QRadialGradient g(QPointF(c.x() - 2.5, c.y() - 2.5), 10.0);
        g.setColorAt(0.0, QColor(140, 222, 108));
        g.setColorAt(0.6, QColor(52, 142, 62));
        g.setColorAt(1.0, QColor(20, 80, 32));
        p.setPen(QPen(QColor(10, 50, 20), 1.2));
        p.setBrush(g);
        p.drawEllipse(c, 7.2, 7.2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 70));
        p.drawEllipse(QPointF(c.x() - 2.6, c.y() - 2.8), 1.8, 1.4);
    }
}

void MainWindow::drawBlades(QImage &img)
{
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto &owner : birds) {
        for (const auto &b : owner.blades) {
            float tipX = b.x * CELL;
            float cy = (b.y + 0.5f) * CELL;
            float len = cfg.bladeLength * CELL;
            QPolygonF feather;
            feather << QPointF(tipX, cy)
                    << QPointF(tipX - len * 0.7f, cy - 5)
                    << QPointF(tipX - len, cy)
                    << QPointF(tipX - len * 0.7f, cy + 5);
            painter.setPen(QPen(QColor(120, 200, 255), 1));
            painter.setBrush(QColor(235, 248, 255));
            painter.drawPolygon(feather);
        }
    }
    painter.end();
}

void MainWindow::drawBladeHud(QImage &img, const Bird &b)
{
    if (!cfg.bladesEnabled) return;
    int cx = (gridW * CELL) / 2;
    int scoreY = hardMode ? 58 : 40;
    int y = scoreY + 24;
    if (b.hasBlade) {
        drawText(img, "BLADE READY [ALT]", cx, y, 120, 220, 255, 11);
    } else if (!b.blades.empty()) {
        drawText(img, "BLADE", cx, y, 235, 248, 255, 11);
    } else {
        drawText(img, "BLADE at " + QString::number(b.nextBladeScore), cx, y, 150, 150, 150, 11);
    }
    if (hardMode && cannonAlive && cannonHits > 0) {
        drawText(img, "CANNON " + QString::number(cfg.cannonHitsToKill - cannonHits) + " HP",
                 cx, y + 20, 255, 140, 140, 10);
    }
}

void MainWindow::drawEnergyBar(QImage &img, const Bird &b, int centerX, int barW, int y0)
{
    // Horizontal bar: green (>= green zone), yellow, red (< red zone).
    const int barH = 14;
    const int x0 = centerX - barW / 2;
    const float energy = b.energy;

    QColor fill;
    if (energy >= cfg.energyGreenMin)      fill = QColor(60, 200, 80);
    else if (energy >= cfg.energyRedMax)   fill = QColor(240, 200, 40);
    else                                   fill = QColor(220, 50, 50);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 140));
    p.drawRoundedRect(x0 - 3, y0 - 3, barW + 6, barH + 6, 5, 5);
    p.setBrush(QColor(40, 40, 40));
    p.drawRoundedRect(x0, y0, barW, barH, 3, 3);

    int w = (int)(barW * (energy / cfg.energyMax));
    if (w > 0) {
        p.setBrush(fill);
        p.drawRoundedRect(x0, y0, w, barH, 3, 3);
    }
    // Zone markers.
    p.setPen(QColor(255, 255, 255, 170));
    int redX = x0 + (int)(barW * cfg.energyRedMax / cfg.energyMax);
    int grnX = x0 + (int)(barW * cfg.energyGreenMin / cfg.energyMax);
    p.drawLine(redX, y0, redX, y0 + barH);
    p.drawLine(grnX, y0, grnX, y0 + barH);
    p.end();

    if (!canAct(b)) {
        if (barW >= 240)
            drawText(img, "EXHAUSTED", x0 + barW + 55, y0 + barH / 2 + 2, 255, 120, 120, 10);
        else
            drawText(img, "EXHAUSTED", centerX, y0 + barH / 2 + 2, 255, 255, 255, 8);
    }
}

void MainWindow::drawPlayerHud(QImage &img)
{
    // Multiplayer HUD: one column per player (name, lives, score, energy, blade).
    static const QColor tagColors[my_label::MAX_PLAYERS] = {
        QColor(255, 214, 0), QColor(90, 190, 255), QColor(255, 90, 140), QColor(120, 230, 90) };
    const int n = (int)birds.size();
    const int W = gridW * CELL;
    const int slotW = W / n;

    for (const auto &b : birds) {
        const int cx = slotW * b.id + slotW / 2;
        const QColor &col = tagColors[b.id % my_label::MAX_PLAYERS];
        const int barW = qMax(60, qMin(190, slotW - 30));
        drawEnergyBar(img, b, cx, barW, 8);

        QString line = QString("P%1").arg(b.id + 1);
        if (b.alive) {
            if (cfg.livesPerPlayer > 1)
                line += "  " + QString(b.lives, QChar(0x2665));        // hearts
            line += "  " + QString::number(b.score);
        } else {
            line += "  OUT  " + QString::number(b.score);
        }
        drawText(img, line, cx, 38, col.red(), col.green(), col.blue(), 11);

        if (cfg.bladesEnabled && b.alive) {
            if (b.hasBlade)
                drawText(img, "BLADE READY", cx, 56, 120, 220, 255, 9);
            else if (!b.blades.empty())
                drawText(img, "BLADE", cx, 56, 235, 248, 255, 9);
            else
                drawText(img, "BLADE at " + QString::number(b.nextBladeScore), cx, 56, 150, 150, 150, 9);
        }
    }

    const int cx = W / 2;
    if (cfg.mpMatchType == 1)
        drawText(img, QString::number(coopScore), cx, 96, 255, 255, 255, 24);
    else if (cfg.mpTargetScore > 0)
        drawText(img, "FIRST TO " + QString::number(cfg.mpTargetScore), cx, 84, 220, 220, 220, 10);
    if (hardMode && cannonAlive && cannonHits > 0)
        drawText(img, "CANNON " + QString::number(cfg.cannonHitsToKill - cannonHits) + " HP",
                 cx, 128, 255, 140, 140, 10);
}

void MainWindow::drawGround(QImage &img)
{
    const int W = img.width();
    const int H = img.height();
    const int gt = (gridH - groundH) * CELL;
    const double s = bgScroll * CELL;     // continuous, never wrapped

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    auto hashU = [](int i, int salt) -> unsigned {
        unsigned x = (unsigned)i * 374761393u + (unsigned)salt * 668265263u;
        x = (x ^ (x >> 13)) * 1274126177u;
        return x ^ (x >> 16);
    };
    auto frac = [&](int i, int salt) { return (hashU(i, salt) & 0xFFFF) / 65535.0f; };

    // Dark earth, fading to near-black with depth.
    QLinearGradient soil(0, gt, 0, H);
    soil.setColorAt(0.0, QColor(20, 26, 22));
    soil.setColorAt(1.0, QColor(6, 8, 8));
    p.fillRect(0, gt, W, H - gt, soil);

    // A few soft stones.
    {
        const float slot = 70.0f;
        const int i0 = (int)std::floor(s / slot) - 1;
        const int i1 = (int)std::floor((s + W) / slot) + 2;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(28, 34, 30));
        for (int i = i0; i <= i1; i++) {
            float x = (float)(i * slot + frac(i, 5) * slot - s);
            float y = gt + 6 + frac(i, 6) * (H - gt - 9);
            p.drawEllipse(QPointF(x, y), 2.5f + 3.0f * frac(i, 7), 1.2f + 1.0f * frac(i, 8));
        }
    }

    // Mossy rim following a continuous wavy edge.
    {
        QPolygonF rim;
        rim << QPointF(-8, gt + 7);
        for (int x = -8; x <= W + 8; x += 5) {
            double wx = x + s;
            double top = 2.0 + 2.2 * (0.5 + 0.5 * std::sin(wx * 0.19))
                             + 1.4 * (0.5 + 0.5 * std::sin(wx * 0.53 + 1.3));
            rim << QPointF(x, gt - top + 2);
        }
        rim << QPointF(W + 8, gt + 7);
        QLinearGradient mg(0, gt - 4, 0, gt + 7);
        mg.setColorAt(0.0, QColor(36, 68, 50));
        mg.setColorAt(1.0, QColor(17, 34, 27));
        p.setPen(Qt::NoPen);
        p.setBrush(mg);
        p.drawPolygon(rim);
    }

    // Curved grass blades.
    {
        const float slot = 12.0f;
        const int i0 = (int)std::floor(s / slot) - 1;
        const int i1 = (int)std::floor((s + W) / slot) + 2;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 58, 42));
        for (int i = i0; i <= i1; i++) {
            float x = (float)(i * slot + frac(i, 1) * 6.0f - s);
            float h = 3.5f + 5.5f * frac(i, 2);
            float lean = (frac(i, 3) - 0.5f) * 5.0f;
            QPainterPath b;
            b.moveTo(x, gt + 2);
            b.quadTo(x + 1.5f, gt - h * 0.6f, x + 2.5f + lean, gt - h);
            b.quadTo(x + 3.2f, gt - h * 0.4f, x + 5.0f, gt + 2);
            b.closeSubpath();
            p.drawPath(b);
        }
    }

    p.setPen(QPen(QColor(4, 8, 7), 1));
    p.drawLine(0, gt + 7, W, gt + 7);

    // Cached vignette over the whole backdrop for a closed-in, tense feel.
    if (vigCache.size() != img.size()) {
        QImage v(W, H, QImage::Format_ARGB32_Premultiplied);
        v.fill(Qt::transparent);
        QPainter vp(&v);
        QRadialGradient vig(W / 2.0, H * 0.5, qMax(W, H) * 0.75);
        vig.setColorAt(0.50, QColor(0, 0, 0, 0));
        vig.setColorAt(1.0, QColor(0, 0, 0, 150));
        vp.fillRect(0, 0, W, H, vig);
        vp.end();
        vigCache = v;
    }
    p.drawImage(0, 0, vigCache);
}

void MainWindow::drawText(QImage &img, const QString &text, int centerX, int centerY,
                           int r, int g, int b, int fontSize)
{
    QPainter painter(&img);
    painter.setPen(QColor(r, g, b));
    QFont font("Arial", fontSize, QFont::Bold);
    painter.setFont(font);

    QFontMetrics fm(font);
    int tw = fm.horizontalAdvance(text);
    int th = fm.height();
    painter.drawText(centerX - tw / 2, centerY + th / 4, text);
    painter.end();
}

void MainWindow::drawHeading(QImage &img)
{
    const int fontSize = 22;
    QPainter painter(&img);
    QFont font("Arial", fontSize, QFont::Bold);
    painter.setFont(font);
    QFontMetrics fm(font);
    int total = fm.horizontalAdvance(headingText);
    int left = (img.width()/5 - total/2);
    int baseY = 28;

    for (int i = 0; i < headingText.size(); i++) {
        QChar ch = headingText[i];
        int x = left + fm.horizontalAdvance(headingText.left(i));
        bool show = headingInPlace[i];
        int y = baseY;
        if (i == headingRefillSlot && headingRefillWait <= 0) {
            show = true;
            y = (int)(baseY - (1.0f - headingRefillT) * 18);
            painter.setPen(QColor(40, 160, 70, (int)(80 + headingRefillT * 175)));
        } else if (show) {
            painter.setPen(QColor(40, 160, 70));
        }
        if (show)
            painter.drawText(x, y, QString(ch));
    }
    painter.end();
}

void MainWindow::drawFallingLetters(QImage &img)
{
    QPainter painter(&img);
    QFont font("Arial", 18, QFont::Bold);
    painter.setFont(font);
    painter.setPen(QColor(30, 140, 55));
    for (const auto &L : fallingLetters) {
        int px = (int)(L.x * CELL);
        int py = (int)(L.y * CELL) + 16;
        painter.drawText(px, py, QString(L.ch));
    }
    painter.end();
}

void MainWindow::drawComicIntroScreen(QImage &img)
{
    if (comicPanel < 0 || comicPanel >= (int)comicPanels.size() || comicPanels[comicPanel].isNull()) {
        img.fill(Qt::black);
        return;
    }

    // A panel whose aspect ratio differs from the window gets a dimmed, enlarged copy
    // of itself behind it instead of plain black bars. Composing that takes two smooth
    // scalings, so it is done once per panel/size and re-used for every frame.
    if (comicIntroCache.size() != img.size() || comicIntroCachePanel != comicPanel) {
        QImage composed(img.size(), QImage::Format_RGB32);
        composed.fill(Qt::black);
        QPainter cachePainter(&composed);
        const QPixmap &panel = comicPanels[comicPanel];

        // Shrink hard and enlarge again: a cheap blur, so the bars read as soft
        // atmosphere rather than a ghosted duplicate of the panel.
        const QSize tiny(qMax(2, img.width() / 24), qMax(2, img.height() / 24));
        const QPixmap backdrop = panel.scaled(tiny, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                                      .scaled(img.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        cachePainter.setOpacity(0.45);
        cachePainter.drawPixmap((img.width() - backdrop.width()) / 2,
                                (img.height() - backdrop.height()) / 2, backdrop);
        cachePainter.setOpacity(1.0);

        const QPixmap scaled = panel.scaled(img.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cachePainter.drawPixmap((img.width() - scaled.width()) / 2,
                                (img.height() - scaled.height()) / 2, scaled);
        cachePainter.end();

        comicIntroCache = composed;
        comicIntroCachePanel = comicPanel;
    }

    QPainter painter(&img);
    painter.drawImage(0, 0, comicIntroCache);
    painter.end();
}

void MainWindow::drawComicBlackScreen(QImage &img)
{
    img.fill(Qt::black);
}

void MainWindow::drawComicVideoScreen(QImage &img)
{
    img.fill(Qt::black);
    if (!comicVideoFrame.isValid()) return;

    QVideoFrame frame = comicVideoFrame;
    if (!frame.map(QVideoFrame::ReadOnly)) return;
    QImage videoImage = frame.toImage();
    frame.unmap();
    if (videoImage.isNull()) return;

    QPainter painter(&img);
    QImage scaled = videoImage.scaled(img.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int x = (img.width() - scaled.width()) / 2;
    int y = (img.height() - scaled.height()) / 2;
    painter.drawImage(x, y, scaled);
    painter.end();
}

void MainWindow::drawComicWhiteFadeScreen(QImage &img)
{
    // Hold pure white for 0.5 s, then fade smoothly to black.
    float fadeT = 0.0f;
    if (comicTransitionMs > COMIC_WHITE_MS)
        fadeT = qBound(0.0f, (comicTransitionMs - COMIC_WHITE_MS) /
                                   (float)COMIC_WHITE_FADE_MS, 1.0f);
    int value = 255 - (int)(255.0f * fadeT);
    img.fill(QColor(value, value, value));
}

void MainWindow::drawTypewriterScreen(QImage &img)
{
    img.fill(Qt::black);
    const QString question = QStringLiteral("Can Jero survive?");
    QString shown = question.left(typewriterCharsShown);
    if (typewriterCharsShown < question.size() || ((typewriterWait / 250) % 2 == 0))
        shown += QLatin1Char('_');

    QPainter painter(&img);
    painter.setPen(Qt::white);
    QFont font("Courier New", 28, QFont::Normal);
    painter.setFont(font);
    QFontMetrics fm(font);
    int x = (img.width() - fm.horizontalAdvance(shown)) / 2;
    int y = img.height() / 2;
    painter.drawText(x, y, shown);
    painter.end();
}

void MainWindow::drawModeSelectScreen(QImage &img)
{
    int cx = (gridW * CELL) / 2;
    int cy = (gridH * CELL) / 4;
    drawText(img, "BIRDS VS PLANTS", cx, cy, 40, 180, 70, 26);
    drawText(img, "Choose Easy / Hard / Troll / Multiplayer", cx, cy + 50, 210, 230, 200, 14);

    // The same animated bird as in-game, flapping gently and bobbing.
    BirdPose s;
    s.k = 2.0f;
    s.cx = (float)cx;
    s.cy = ((gridH - groundH) * CELL) / 2.0f + std::sin(bobTimer) * 4.0f;
    s.mode = WM_CRUISE;
    s.phase = bobTimer * 2.2f;
    s.tilt = std::cos(bobTimer) * 4.0f;
    QPainter p(&img);
    paintBird(p, s);
}

void MainWindow::drawStartScreen(QImage &img)
{
    drawModeSelectScreen(img);
}

void MainWindow::drawIntroPromptScreen(QImage &img)
{
    img.fill(Qt::black);
    int centerX = img.width() / 2;
    int centerY = img.height() / 2;
    drawText(img, "BIRDS VS PLANTS", centerX, centerY - 30, 40, 180, 70, 26);
    drawText(img, "Press SPACE to begin", centerX, centerY + 25, 230, 230, 230, 16);
}

void MainWindow::drawGameOverScreen(QImage &img)
{
    int cx = (gridW * CELL) / 2;
    int cy = (gridH * CELL) / 3;

    if (multiplayer) {
        drawMultiplayerOver(img);
        return;
    }
    drawText(img, "GAME OVER", cx, cy, 255, 60, 60, 26);
    drawText(img, "Score: " + QString::number(score), cx, cy + 50, 255, 255, 255, 18);
    drawText(img, "Best: " + QString::number(highScore), cx, cy + 85, 255, 220, 0, 16);
    drawText(img, "SPACE: retry", cx, cy + 130, 200, 200, 200, 13);
    drawText(img, "Restart: choose mode", cx, cy + 155, 200, 200, 200, 13);
}

void MainWindow::drawMultiplayerOver(QImage &img)
{
    static const QColor tagColors[my_label::MAX_PLAYERS] = {
        QColor(255, 214, 0), QColor(90, 190, 255), QColor(255, 90, 140), QColor(120, 230, 90) };
    const int cx = (gridW * CELL) / 2;
    int y = (gridH * CELL) / 4;

    QPainter painter(&img);
    painter.fillRect(QRect(cx - 190, y - 40, 380, 60 + 28 * (int)birds.size() + 120), QColor(0, 0, 0, 150));
    painter.end();

    if (cfg.mpMatchType == 1) {
        drawText(img, "GAME OVER", cx, y, 255, 60, 60, 26);
        drawText(img, "Team score: " + QString::number(coopScore), cx, y + 40, 255, 255, 255, 18);
    } else if (winner >= 0) {
        const QColor &col = tagColors[winner % my_label::MAX_PLAYERS];
        drawText(img, QString("PLAYER %1 WINS!").arg(winner + 1), cx, y, col.red(), col.green(), col.blue(), 26);
    } else {
        drawText(img, "DRAW", cx, y, 255, 220, 0, 26);
    }

    y += 80;
    for (const auto &b : birds) {
        const QColor &col = tagColors[b.id % my_label::MAX_PLAYERS];
        drawText(img, QString("P%1:  %2").arg(b.id + 1).arg(b.score), cx, y, col.red(), col.green(), col.blue(), 15);
        y += 28;
    }
    y += 12;
    drawText(img, "Best: " + QString::number(highScore), cx, y, 255, 220, 0, 13);
    if (gameOverMs >= cfg.mpGameOverLockMs)
        drawText(img, "Any flap key: rematch", cx, y + 30, 200, 200, 200, 13);
    drawText(img, "Restart: choose mode", cx, y + 55, 200, 200, 200, 13);
}

void MainWindow::drawFlash(QImage &img)
{
    if (flashMsLeft <= 0) return;
    QPainter painter(&img);
    int alpha = 255;
    if (flashMsLeft < 200)
        alpha = flashMsLeft * 255 / 200;
    painter.fillRect(img.rect(), QColor(255, 255, 255, alpha));
    painter.end();
}

void MainWindow::drawTrollOverlay(QImage &img)
{
    QPainter painter(&img);
    painter.fillRect(img.rect(), QColor(0, 0, 0, 170));
    painter.end();
    int cx = (gridW * CELL) / 2;
    int cy = (gridH * CELL) / 2;
    drawText(img, "GAME OVER", cx, cy - 15, 255, 60, 60, 28);
    //drawText(img, "...SIKE", cx, cy + 30, 255, 220, 0, 16);
}

void MainWindow::drawWind(QImage &img)
{
    if (windX == 0.0f || img.width() <= 0 || img.height() <= 0) return;

    const int direction = windX > 0.0f ? 1 : -1;
    const int cycleWidth = img.width() + 96;
    const int alpha = 70 + (int)(std::fabs(windX) / 4.6f * 130.0f);
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);

    for (int i = 0; i < 12; i++) {
        int y = (i * 37 + 18) % img.height();
        int phase = (windAnimMs * 2 + i * 71) % cycleWidth;
        int startX = direction > 0 ? phase - 48 : img.width() - phase + 48;
        int endX = startX + direction * (24 + (i % 3) * 8);
        painter.setPen(QPen(QColor(205, 245, 255, alpha), 2,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(startX, y, endX, y);
        painter.drawLine(endX - direction * 7, y - 4, endX, y);
        painter.drawLine(endX - direction * 7, y + 4, endX, y);
    }

    painter.end();
}

void MainWindow::sendLanInit(int peerId)
{
    // Everything a joinee needs to render that does not change during a round:
    // the match settings and the world size. Sent once per round and to each peer
    // that connects, instead of being repeated in every 30 Hz state snapshot.
    if (!lanNetwork || !lanNetwork->isHost()) return;
    QJsonObject init{
        {QStringLiteral("type"), QStringLiteral("lan_init")},
        {QStringLiteral("hardMode"), hardMode}, {QStringLiteral("trollMode"), trollMode},
        {QStringLiteral("multiplayer"), multiplayer}, {QStringLiteral("matchActive"), lanMatchActive},
        {QStringLiteral("gridW"), gridW}, {QStringLiteral("gridH"), gridH},
        {QStringLiteral("groundH"), groundH}, {QStringLiteral("pipeWidth"), pipeWidth},
        {QStringLiteral("pipeGap"), pipeGap}, {QStringLiteral("pipeSpacing"), pipeSpacing},
        {QStringLiteral("homeX"), homeX}, {QStringLiteral("config"), gameConfigToJson(cfg)}
    };
    if (peerId >= 1) lanNetwork->sendMessageToPeer(peerId, init);
    else lanNetwork->sendMessage(init);
}

void MainWindow::sendLanInput(const QString &action, bool held)
{
    if (!lanNetwork || !lanNetwork->isConnected()) return;
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("input")},
                        {QStringLiteral("action"), action},
                        {QStringLiteral("held"), held},
                        {QStringLiteral("playerId"), lanLocalPlayerId}};
    lanNetwork->sendMessage(message);
}

void MainWindow::handleLanMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("welcome") && lanClientMode) {
        lanLocalPlayerId = message.value(QStringLiteral("playerId")).toInt(1);
        ui->statusLabel->setText(QStringLiteral("Joined LAN lobby as Player %1 — waiting for host to start…").arg(lanLocalPlayerId + 1));
        return;
    }
    // Static game configuration is sent once to each joining peer, including peers
    // that join after the host has already started a round.
    if (type == QLatin1String("hello") && lanNetwork && lanNetwork->isHost()) {
        const int peerId = message.value(QStringLiteral("_peerId")).toInt(-1);
        if (peerId >= 1) sendLanInit(peerId);
        return;
    }
    if (type == QLatin1String("lan_init") && lanClientMode) {
        // Music is a local preference: never let the host's values replace ours.
        const bool keepMusicOn = cfg.musicEnabled;
        const float keepMusicVol = cfg.musicVolume;
        if (message.contains(QStringLiteral("config")))
            gameConfigFromJson(cfg, message.value(QStringLiteral("config")).toObject());
        cfg.musicEnabled = keepMusicOn;
        cfg.musicVolume = keepMusicVol;
        if (message.contains(QStringLiteral("hardMode"))) hardMode = message.value(QStringLiteral("hardMode")).toBool(hardMode);
        if (message.contains(QStringLiteral("trollMode"))) trollMode = message.value(QStringLiteral("trollMode")).toBool(trollMode);
        if (message.contains(QStringLiteral("multiplayer"))) multiplayer = message.value(QStringLiteral("multiplayer")).toBool(multiplayer);
        gridW = message.value(QStringLiteral("gridW")).toInt(gridW);
        gridH = message.value(QStringLiteral("gridH")).toInt(gridH);
        groundH = message.value(QStringLiteral("groundH")).toInt(groundH);
        pipeWidth = message.value(QStringLiteral("pipeWidth")).toInt(pipeWidth);
        pipeGap = message.value(QStringLiteral("pipeGap")).toInt(pipeGap);
        pipeSpacing = message.value(QStringLiteral("pipeSpacing")).toInt(pipeSpacing);
        homeX = message.value(QStringLiteral("homeX")).toInt(homeX);
        lanMatchActive = message.value(QStringLiteral("matchActive")).toBool(false);
        return;
    }
    if (type == QLatin1String("reject")) {
        QMessageBox::warning(this, QStringLiteral("LAN game"), message.value(QStringLiteral("reason")).toString(QStringLiteral("The host rejected the connection.")));
        lanNetwork->disconnectFromPeer();
        lanClientMode = false;
        return;
    }
    if (type == QLatin1String("input") && lanNetwork->isHost() && lanMatchActive) {
        const int player = message.value(QStringLiteral("_peerId")).toInt(-1);
        if (player < 1 || player >= (int)birds.size()) return;
        const QString action = message.value(QStringLiteral("action")).toString();
        const bool held = message.value(QStringLiteral("held")).toBool();
        dispatchingLanInput = true;
        if (action == QLatin1String("flap")) onFlap(player);
        else if (action == QLatin1String("shoot")) onShoot(player);
        else if (action == QLatin1String("thrust")) onThrustHeld(player, held);
        else if (action == QLatin1String("brake")) onBrakeHeld(player, held);
        else if (action == QLatin1String("restart") && state == GameOver) beginPlay(hardMode, trollMode, true);
        dispatchingLanInput = false;
    } else if (type == QLatin1String("state") && lanClientMode) {
        applyLanState(message);
    }
}

void MainWindow::broadcastLanState()
{
    if (!lanNetwork || !lanNetwork->isHost() || !lanNetwork->isConnected()) return;

    // Bound snapshot traffic to ~30 Hz. TCP is a stream, so limiting the rate here
    // also saves JSON work. The next game tick always sends a fresh snapshot, so a
    // skipped call never loses information.
    static QElapsedTimer snapshotClock;
    if (!snapshotClock.isValid()) snapshotClock.start();
    if (snapshotClock.elapsed() < LanSnapshotMinIntervalMs) return;
    snapshotClock.restart();

    QJsonArray birdArray;
    for (const Bird &b : birds) {
        QJsonArray bladeArray;
        for (const FeatherBlade &blade : b.blades)
            bladeArray.append(QJsonObject{{QStringLiteral("x"), blade.x}, {QStringLiteral("y"), blade.y}});
        birdArray.append(QJsonObject{
            {QStringLiteral("id"), b.id}, {QStringLiteral("x"), b.x}, {QStringLiteral("y"), b.y},
            {QStringLiteral("vel"), b.vel}, {QStringLiteral("scale"), b.scale}, {QStringLiteral("scaleMs"), b.scaleMs},
            {QStringLiteral("energy"), b.energy}, {QStringLiteral("speedMul"), b.speedMul},
            {QStringLiteral("thrustKey"), b.thrustKey}, {QStringLiteral("brakeKey"), b.brakeKey},
            {QStringLiteral("thrustActive"), b.thrustActive}, {QStringLiteral("brakeActive"), b.brakeActive},
            {QStringLiteral("wingPhase"), b.wingPhase}, {QStringLiteral("tilt"), b.tilt},
            {QStringLiteral("flapAnimMs"), b.flapAnimMs}, {QStringLiteral("shootAnimMs"), b.shootAnimMs},
            {QStringLiteral("hasBlade"), b.hasBlade}, {QStringLiteral("nextBladeScore"), b.nextBladeScore},
            {QStringLiteral("blades"), bladeArray}, {QStringLiteral("score"), b.score},
            {QStringLiteral("lives"), b.lives}, {QStringLiteral("alive"), b.alive},
            {QStringLiteral("invulnMs"), b.invulnMs}, {QStringLiteral("diedAtMs"), b.diedAtMs},
            {QStringLiteral("hue"), b.hue}
        });
    }

    QJsonArray pipeArray;
    for (const Pipe &p : pipes) {
        QJsonArray gaps;
        for (const auto &gap : p.extraGaps) gaps.append(QJsonArray{gap.first, gap.second});
        pipeArray.append(QJsonObject{
            {QStringLiteral("x"), p.x}, {QStringLiteral("gapTop"), p.gapTop},
            {QStringLiteral("gapHeight"), p.gapHeight}, {QStringLiteral("scored"), p.scored},
            {QStringLiteral("scoredMask"), (int)p.scoredMask}, {QStringLiteral("hasEater"), p.hasEater},
            {QStringLiteral("fakePipe"), p.fakePipe}, {QStringLiteral("fakeActivated"), p.fakeActivated},
            {QStringLiteral("gapShifted"), p.gapShifted}, {QStringLiteral("originalGapTop"), p.originalGapTop},
            {QStringLiteral("windAffected"), p.windAffected}, {QStringLiteral("eaterFromTop"), p.eaterFromTop},
            {QStringLiteral("eaterTriggered"), p.eaterTriggered}, {QStringLiteral("seed"), p.seed},
            {QStringLiteral("eaterOut"), p.eaterOut}, {QStringLiteral("eaterMax"), p.eaterMax},
            {QStringLiteral("extraGaps"), gaps}
        });
    }

    QJsonArray ballArray;
    for (const CactusBall &ball : cactusBalls)
        ballArray.append(QJsonObject{{QStringLiteral("x"), ball.x}, {QStringLiteral("y"), ball.y},
                                     {QStringLiteral("vx"), ball.vx}, {QStringLiteral("vy"), ball.vy}});

    QJsonArray lettersArray;
    for (const FallingLetter &letter : fallingLetters)
        lettersArray.append(QJsonObject{{QStringLiteral("ch"), QString(1, letter.ch)},
                                        {QStringLiteral("slotIndex"), letter.slotIndex},
                                        {QStringLiteral("x"), letter.x}, {QStringLiteral("y"), letter.y},
                                        {QStringLiteral("velY"), letter.velY}, {QStringLiteral("grounded"), letter.grounded}});
    QJsonArray headingArray;
    for (char present : headingInPlace) headingArray.append((int)present);

    QJsonObject message{
        {QStringLiteral("type"), QStringLiteral("state")},
        // Compact per-frame snapshot: only what changes while a round is running.
        // Match settings and world size travel in lan_init (see sendLanInit).
        {QStringLiteral("stateCode"), (int)state},
        {QStringLiteral("pipeSpeed"), pipeSpeed},
        {QStringLiteral("groundOffset"), groundOffset}, {QStringLiteral("bgScroll"), bgScroll},
        {QStringLiteral("score"), score}, {QStringLiteral("highScore"), highScore},
        {QStringLiteral("coopScore"), coopScore}, {QStringLiteral("playMs"), playMs},
        {QStringLiteral("gameOverMs"), gameOverMs}, {QStringLiteral("winner"), winner},
        {QStringLiteral("bobTimer"), bobTimer}, {QStringLiteral("countdownMs"), lanCountdownMs},
        {QStringLiteral("flashMsLeft"), flashMsLeft}, {QStringLiteral("trollPauseMs"), trollPauseMs},
        {QStringLiteral("windX"), windX},
        {QStringLiteral("windAnimMs"), windAnimMs}, {QStringLiteral("fakeGapMs"), fakeGapMs},
        {QStringLiteral("cannonX"), cannonX}, {QStringLiteral("cannonY"), cannonY},
        {QStringLiteral("cannonVelX"), cannonVelX}, {QStringLiteral("cannonVelY"), cannonVelY},
        {QStringLiteral("cannonAimX"), cannonAimX}, {QStringLiteral("cannonAimY"), cannonAimY},
        {QStringLiteral("cannonFireMs"), cannonFireMs}, {QStringLiteral("cannonDirectionMs"), cannonDirectionMs},
        {QStringLiteral("cannonHits"), cannonHits}, {QStringLiteral("cannonAlive"), cannonAlive},
        // The title-letter refill animation is drawn from these, so joinees need them.
        {QStringLiteral("headingRefillSlot"), headingRefillSlot},
        {QStringLiteral("headingRefillT"), headingRefillT}, {QStringLiteral("headingRefillWait"), headingRefillWait},
        {QStringLiteral("headingInPlace"), headingArray},
        {QStringLiteral("birds"), birdArray}, {QStringLiteral("pipes"), pipeArray},
        {QStringLiteral("cactusBalls"), ballArray}, {QStringLiteral("fallingLetters"), lettersArray}
    };
    lanNetwork->sendMessage(message);
}

void MainWindow::applyLanState(const QJsonObject &message)
{
    // Remember where things were drawn so rendering can blend toward the new positions.
    lanPreviousBirdPositions.clear();
    lanPreviousBirdPositions.reserve(birds.size());
    for (const Bird &bird : birds)
        lanPreviousBirdPositions.emplace_back(bird.x, bird.y);
    lanPreviousPipeX.clear();
    lanPreviousPipeX.reserve(pipes.size());
    for (const Pipe &pipe : pipes) lanPreviousPipeX.push_back(pipe.x);
    const GameState previousState = state;

    // Music is a local preference: never let the host's values replace ours.
    const bool keepMusicOn = cfg.musicEnabled;
    const float keepMusicVol = cfg.musicVolume;
    // Compact snapshots omit the configuration (it arrives in lan_init). When a
    // snapshot does carry it (e.g. from a host running the older protocol), apply it.
    if (message.contains(QStringLiteral("config")))
        gameConfigFromJson(cfg, message.value(QStringLiteral("config")).toObject());
    cfg.musicEnabled = keepMusicOn;
    cfg.musicVolume = keepMusicVol;
    if (message.contains(QStringLiteral("hardMode"))) hardMode = message.value(QStringLiteral("hardMode")).toBool(hardMode);
    if (message.contains(QStringLiteral("trollMode"))) trollMode = message.value(QStringLiteral("trollMode")).toBool(trollMode);
    if (message.contains(QStringLiteral("multiplayer"))) multiplayer = message.value(QStringLiteral("multiplayer")).toBool(multiplayer);
    lanMatchActive = true;

    gridW = message.value(QStringLiteral("gridW")).toInt(gridW);
    gridH = message.value(QStringLiteral("gridH")).toInt(gridH);
    groundH = message.value(QStringLiteral("groundH")).toInt(groundH);
    pipeWidth = message.value(QStringLiteral("pipeWidth")).toInt(pipeWidth);
    pipeGap = message.value(QStringLiteral("pipeGap")).toInt(pipeGap);
    pipeSpacing = message.value(QStringLiteral("pipeSpacing")).toInt(pipeSpacing);
    pipeSpeed = (float)message.value(QStringLiteral("pipeSpeed")).toDouble(pipeSpeed);
    distSinceLastPipe = (float)message.value(QStringLiteral("distSinceLastPipe")).toDouble(distSinceLastPipe);
    pipesSpawned = message.value(QStringLiteral("pipesSpawned")).toInt(pipesSpawned);
    homeX = message.value(QStringLiteral("homeX")).toInt(homeX);
    groundOffset = (float)message.value(QStringLiteral("groundOffset")).toDouble(groundOffset);
    bgScroll = message.value(QStringLiteral("bgScroll")).toDouble(bgScroll);
    score = message.value(QStringLiteral("score")).toInt(score);
    highScore = message.value(QStringLiteral("highScore")).toInt(highScore);
    coopScore = message.value(QStringLiteral("coopScore")).toInt(coopScore);
    playMs = message.value(QStringLiteral("playMs")).toInt(playMs);
    gameOverMs = message.value(QStringLiteral("gameOverMs")).toInt(gameOverMs);
    winner = message.value(QStringLiteral("winner")).toInt(winner);
    bobTimer = (float)message.value(QStringLiteral("bobTimer")).toDouble(bobTimer);
    lanCountdownMs = message.value(QStringLiteral("countdownMs")).toInt(0);
    hardPlayMs = message.value(QStringLiteral("hardPlayMs")).toInt(hardPlayMs);
    flashMsLeft = message.value(QStringLiteral("flashMsLeft")).toInt(flashMsLeft);
    trollPauseMs = message.value(QStringLiteral("trollPauseMs")).toInt(trollPauseMs);
    trollEventMs = message.value(QStringLiteral("trollEventMs")).toInt(trollEventMs);
    windX = (float)message.value(QStringLiteral("windX")).toDouble(windX);
    windAnimMs = message.value(QStringLiteral("windAnimMs")).toInt(windAnimMs);
    fakeGapMs = message.value(QStringLiteral("fakeGapMs")).toInt(fakeGapMs);
    cannonX = (float)message.value(QStringLiteral("cannonX")).toDouble(cannonX);
    cannonY = (float)message.value(QStringLiteral("cannonY")).toDouble(cannonY);
    cannonVelX = (float)message.value(QStringLiteral("cannonVelX")).toDouble(cannonVelX);
    cannonVelY = (float)message.value(QStringLiteral("cannonVelY")).toDouble(cannonVelY);
    cannonAimX = (float)message.value(QStringLiteral("cannonAimX")).toDouble(cannonAimX);
    cannonAimY = (float)message.value(QStringLiteral("cannonAimY")).toDouble(cannonAimY);
    cannonFireMs = message.value(QStringLiteral("cannonFireMs")).toInt(cannonFireMs);
    cannonDirectionMs = message.value(QStringLiteral("cannonDirectionMs")).toInt(cannonDirectionMs);
    cannonHits = message.value(QStringLiteral("cannonHits")).toInt(cannonHits);
    cannonAlive = message.value(QStringLiteral("cannonAlive")).toBool(cannonAlive);
    headingDropDelay = message.value(QStringLiteral("headingDropDelay")).toInt(headingDropDelay);
    headingRefillSlot = message.value(QStringLiteral("headingRefillSlot")).toInt(headingRefillSlot);
    headingRefillT = (float)message.value(QStringLiteral("headingRefillT")).toDouble(headingRefillT);
    headingRefillWait = message.value(QStringLiteral("headingRefillWait")).toInt(headingRefillWait);

    birds.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("birds")).toArray()) {
        const QJsonObject o = value.toObject();
        Bird b{};
        b.id = o.value(QStringLiteral("id")).toInt();
        b.x = (float)o.value(QStringLiteral("x")).toDouble(); b.y = (float)o.value(QStringLiteral("y")).toDouble();
        b.vel = (float)o.value(QStringLiteral("vel")).toDouble(); b.scale = o.value(QStringLiteral("scale")).toInt(1);
        b.scaleMs = o.value(QStringLiteral("scaleMs")).toInt(); b.energy = (float)o.value(QStringLiteral("energy")).toDouble(100.0);
        b.speedMul = (float)o.value(QStringLiteral("speedMul")).toDouble(1.0);
        b.thrustKey = o.value(QStringLiteral("thrustKey")).toBool(); b.brakeKey = o.value(QStringLiteral("brakeKey")).toBool();
        b.thrustActive = o.value(QStringLiteral("thrustActive")).toBool(); b.brakeActive = o.value(QStringLiteral("brakeActive")).toBool();
        b.wingPhase = (float)o.value(QStringLiteral("wingPhase")).toDouble(); b.tilt = (float)o.value(QStringLiteral("tilt")).toDouble();
        b.flapAnimMs = o.value(QStringLiteral("flapAnimMs")).toInt(); b.shootAnimMs = o.value(QStringLiteral("shootAnimMs")).toInt();
        b.hasBlade = o.value(QStringLiteral("hasBlade")).toBool(); b.nextBladeScore = o.value(QStringLiteral("nextBladeScore")).toInt();
        b.score = o.value(QStringLiteral("score")).toInt(); b.lives = o.value(QStringLiteral("lives")).toInt(1);
        b.alive = o.value(QStringLiteral("alive")).toBool(true); b.invulnMs = o.value(QStringLiteral("invulnMs")).toInt();
        b.diedAtMs = o.value(QStringLiteral("diedAtMs")).toInt(); b.hue = o.value(QStringLiteral("hue")).toInt();
        for (const QJsonValue &bladeValue : o.value(QStringLiteral("blades")).toArray()) {
            const QJsonObject bladeObject = bladeValue.toObject();
            b.blades.push_back(FeatherBlade{(float)bladeObject.value(QStringLiteral("x")).toDouble(),
                                            (float)bladeObject.value(QStringLiteral("y")).toDouble()});
        }
        birds.push_back(b);
    }
    lanTargetBirdPositions.clear();
    lanTargetBirdPositions.reserve(birds.size());
    for (const Bird &bird : birds)
        lanTargetBirdPositions.emplace_back(bird.x, bird.y);
    lanInterpolationElapsedMs = lanPreviousBirdPositions.size() == lanTargetBirdPositions.size()
        && !lanPreviousBirdPositions.empty() ? 0 : LanSnapshotIntervalMs;

    pipes.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("pipes")).toArray()) {
        const QJsonObject o = value.toObject(); Pipe p{};
        p.x = (float)o.value(QStringLiteral("x")).toDouble(); p.gapTop = o.value(QStringLiteral("gapTop")).toInt();
        p.gapHeight = o.value(QStringLiteral("gapHeight")).toInt(); p.scored = o.value(QStringLiteral("scored")).toBool();
        p.scoredMask = (unsigned)o.value(QStringLiteral("scoredMask")).toInt(); p.hasEater = o.value(QStringLiteral("hasEater")).toBool();
        p.fakePipe = o.value(QStringLiteral("fakePipe")).toBool(); p.fakeActivated = o.value(QStringLiteral("fakeActivated")).toBool();
        p.gapShifted = o.value(QStringLiteral("gapShifted")).toBool(); p.originalGapTop = o.value(QStringLiteral("originalGapTop")).toInt();
        p.windAffected = o.value(QStringLiteral("windAffected")).toBool(); p.eaterFromTop = o.value(QStringLiteral("eaterFromTop")).toBool();
        p.eaterTriggered = o.value(QStringLiteral("eaterTriggered")).toBool(); p.seed = o.value(QStringLiteral("seed")).toInt();
        p.eaterOut = (float)o.value(QStringLiteral("eaterOut")).toDouble(); p.eaterMax = (float)o.value(QStringLiteral("eaterMax")).toDouble();
        for (const QJsonValue &gapValue : o.value(QStringLiteral("extraGaps")).toArray()) {
            const QJsonArray gap = gapValue.toArray(); if (gap.size() >= 2) p.extraGaps.emplace_back(gap[0].toInt(), gap[1].toInt());
        }
        pipes.push_back(p);
    }
    lanTargetPipeX.clear();
    lanTargetPipeX.reserve(pipes.size());
    for (const Pipe &pipe : pipes) lanTargetPipeX.push_back(pipe.x);
    if (lanPreviousPipeX.size() != lanTargetPipeX.size())
        lanPreviousPipeX = lanTargetPipeX;

    cactusBalls.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("cactusBalls")).toArray()) {
        const QJsonObject o = value.toObject();
        cactusBalls.push_back(CactusBall{(float)o.value(QStringLiteral("x")).toDouble(),
                                         (float)o.value(QStringLiteral("y")).toDouble(),
                                         (float)o.value(QStringLiteral("vx")).toDouble(),
                                         (float)o.value(QStringLiteral("vy")).toDouble()});
    }
    fallingLetters.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("fallingLetters")).toArray()) {
        const QJsonObject o = value.toObject(); const QString ch = o.value(QStringLiteral("ch")).toString();
        fallingLetters.push_back(FallingLetter{ch.isEmpty() ? QChar() : ch.at(0), o.value(QStringLiteral("slotIndex")).toInt(),
                                               (float)o.value(QStringLiteral("x")).toDouble(), (float)o.value(QStringLiteral("y")).toDouble(),
                                               (float)o.value(QStringLiteral("velY")).toDouble(), o.value(QStringLiteral("grounded")).toBool()});
    }
    headingInPlace.clear();
    for (const QJsonValue &value : message.value(QStringLiteral("headingInPlace")).toArray()) headingInPlace.push_back((char)value.toInt());
    if (headingInPlace.size() != (size_t)headingText.size()) headingInPlace.assign(headingText.size(), 1);

    const int receivedState = message.value(QStringLiteral("stateCode")).toInt((int)Playing);
    state = (GameState)receivedState;
    // The host plays the crash sting when it detects the crash; play it here too when
    // this joinee sees the round end.
    if (state == GameOver && previousState != GameOver)
        playGameOverCrash();
    ui->easyButton->hide(); ui->hardButton->hide(); ui->trollButton->hide();
    ui->multiplayerButton->hide(); ui->localMultiplayerButton->hide(); ui->hostLanButton->hide();
    ui->joinLanButton->hide(); ui->multiplayerBackButton->hide(); ui->settingsButton->hide();
    if (state == GameOver) {
        ui->restartButton->show();
        ui->statusLabel->setText(winner >= 0 ? QStringLiteral("GAME OVER — Player %1 wins. Press Space to restart.").arg(winner + 1)
                                             : QStringLiteral("GAME OVER — draw / co-op ended. Press Space to restart."));
    } else {
        ui->restartButton->hide();
        ui->statusLabel->setText(QStringLiteral("LAN %1 — Player %2 | Space flap, Left brake, Right thrust, Alt shoot")
            .arg(trollMode ? QStringLiteral("Troll") : (hardMode ? QStringLiteral("Hard") : QStringLiteral("Easy")))
            .arg(lanLocalPlayerId + 1));
    }
    ui->highScoreLabel->setText(QStringLiteral("Best: ") + QString::number(highScore));
    refreshScore();
    // Do not render here: the client's game timer renders the latest applied state.
    // Rendering both here and from updateGame() doubled the frame work on every packet.
}

void MainWindow::renderFrame()
{
    // Build a fresh off-screen image and draw the current game state on it.
    // This keeps the visual update simple and avoids painting the UI directly.
    int imgW = gridW * CELL;
    int imgH = gridH * CELL;
    if (imgW <= 0 || imgH <= 0) return;

    QImage img(imgW, imgH, QImage::Format_RGB32);

    drawForestBg(img);
    drawTrees(img);
    drawGround(img);

    if (state == Playing || state == GameOver) {
        drawPipes(img);
        if (hardMode) {
            drawEaters(img);
            if (cannonAlive)
                drawCannon(img);
            drawCactusBalls(img);
        }
        for (const auto &b : birds) {
            // In multiplayer a bird that is out only reappears (limp) on the final screen.
            if (b.alive || state == GameOver)
                drawBird(img, b);
        }
        drawBlades(img);
        if (multiplayer)
            drawBirdNames(img);
        if (trollMode) {
            drawHeading(img);
            drawFallingLetters(img);
        }
    }

    if (state == IntroWait) {
        drawIntroPromptScreen(img);
    } else if (state == ComicIntro) {
        drawComicIntroScreen(img);
    } else if (state == ComicBlackBeforeVideo) {
        drawComicBlackScreen(img);
    } else if (state == ComicVideo) {
        drawComicVideoScreen(img);
    } else if (state == ComicWhiteFade) {
        drawComicWhiteFadeScreen(img);
    } else if (state == Typewriter) {
        drawTypewriterScreen(img);
    } else if (state == ModeSelect) {
        drawModeSelectScreen(img);
    } else if (state == GameOver) {
        drawGameOverScreen(img);
    }

    if (state == Playing) {
        if (trollMode && windX != 0.0f)
            drawWind(img);
        if (trollMode && trollPauseMs > 0)
            drawTrollOverlay(img);
        if (multiplayer) {
            drawPlayerHud(img);
        } else {
            int cx = (gridW * CELL) / 2;
            int scoreY = hardMode ? 58 : 40;
            drawText(img, QString::number(score), cx, scoreY, 255, 255, 255, 24);
            drawBladeHud(img, birds[0]);
            drawEnergyBar(img, birds[0], cx, qMin(260, (gridW * CELL) / 2), 8);
        }
        if (lanCountdownMs > 0) {
            const int readyX = (gridW * CELL) / 2;
            const int readyY = (gridH * CELL) / 2;
            drawText(img, QString::number((lanCountdownMs + 999) / 1000), readyX, readyY - 30, 255, 255, 255, 72);
            drawText(img, QStringLiteral("GET READY"), readyX, readyY + 40, 255, 220, 0, 22);
        }
        drawFlash(img);
    }

    // The finished picture is handed to my_label as-is. When its size differs from the
    // widget (e.g. a LAN joinee showing the host's world), my_label::paintEvent scales
    // and letterboxes it at paint time instead of this function rescaling every frame.
    ui->frame->setPixmap(QPixmap::fromImage(img));
}
