#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QResizeEvent>
#include <QRandomGenerator>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    state(IntroWait),
    gridW(100), gridH(100),
    birdY(0), birdVel(0), birdX(15),
    birdScale(1),
    birdScaleMs(0),
    pipeSpeed(easyPipeSpeed), pipeWidth(easyPipeWidth),
    pipeGap(easyPipeGap), pipeSpacing(easyPipeSpacing),
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
    headingText(QStringLiteral("           FLAPPY BIRD -V.3.0.1")),
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

    comicVideoPlayer->setVideoOutput(comicVideoSink);
    comicVideoPlayer->setSource(QUrl(QStringLiteral("qrc:/comic/assets/after_zero_cinematic.mp4")));
    comicVideoPlayer->setLoops(1);
    connect(comicVideoSink, &QVideoSink::videoFrameChanged, this, &MainWindow::onComicVideoFrameChanged);
    connect(comicVideoPlayer, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            finishComicVideo();
    });

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
    ui->restartButton->hide();

    connect(ui->frame, &my_label::flap, this, &MainWindow::onFlap);
    connect(ui->frame, &my_label::shoot, this, &MainWindow::onShoot);
    connect(ui->easyButton, &QPushButton::clicked, this, &MainWindow::onEasyClicked);
    connect(ui->hardButton, &QPushButton::clicked, this, &MainWindow::onHardClicked);
    connect(ui->trollButton, &QPushButton::clicked, this, &MainWindow::onTrollClicked);
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

void MainWindow::updateGridSize()
{
    int pw = ui->frame->width();
    int ph = ui->frame->height();
    if (pw < CELL) pw = CELL;
    if (ph < CELL) ph = CELL;
    gridW = pw / CELL;
    gridH = ph / CELL;
    birdX = gridW / 5;
    if (birdX < 3) birdX = 3;
}

void MainWindow::applyDifficulty()
{
    if (hardMode) {
        pipeGap = hardPipeGap;
        pipeSpeed = hardPipeSpeed;
        pipeWidth = hardPipeWidth;
        pipeSpacing = hardPipeSpacing;
    } else {
        pipeGap = easyPipeGap;
        pipeSpeed = easyPipeSpeed;
        pipeWidth = easyPipeWidth;
        pipeSpacing = easyPipeSpacing;
    }
}

void MainWindow::updateHardSpeed()
{
    if (!hardMode) return;
    int steps = score / 50;
    if (steps <= 0) {
        pipeSpeed = hardPipeSpeed;
    } else {
        pipeSpeed = hardPipeSpeedFast + (steps - 1) * hardSpeedBump;
    }
}

void MainWindow::showModeSelect()
{
    state = ModeSelect;
    ui->easyButton->show();
    ui->hardButton->show();
    ui->trollButton->show();
    ui->restartButton->hide();
    ui->statusLabel->setText("Choose a mode");
    ui->frame->setFocus();
    renderFrame();
}

void MainWindow::beginPlay(bool hard, bool troll)
{
    hardMode = hard;
    trollMode = troll;
    state = Playing;
    ui->easyButton->hide();
    ui->hardButton->hide();
    ui->trollButton->hide();
    ui->restartButton->hide();
    ui->statusLabel->setText(trollMode ? "Troll Mode" : (hardMode ? "Hard" : "Easy"));
    resetGame();
    ui->frame->setFocus();
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

void MainWindow::onRestartClicked()
{
    showModeSelect();
}

void MainWindow::skipOrAdvanceComic()
{
    if (state == ComicIntro) {
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
    comicVideoStarted = true;
    comicTransitionMs = 0;
    comicVideoFrame = QVideoFrame();
    comicVideoPlayer->setPosition(0);
    comicVideoPlayer->play();
    state = ComicVideo;
}

void MainWindow::finishComicVideo()
{
    if (!comicVideoStarted && state != ComicVideo)
        return;
    comicVideoPlayer->pause();
    comicVideoStarted = false;
    comicTransitionMs = 0;
    state = ComicWhiteFade;
}

void MainWindow::onComicVideoFrameChanged(const QVideoFrame &frame)
{
    comicVideoFrame = frame;
    if (state == ComicVideo)
        renderFrame();
}

void MainWindow::onFlap()
{
    if (state == IntroWait) {
        state = ComicIntro;
        comicPanel = 0;
        comicMs = 0;
        ui->statusLabel->setText("SPACE next");
        renderFrame();
    } else if (state == ComicIntro || state == ComicBlackBeforeVideo || state == ComicVideo || state == ComicWhiteFade || state == Typewriter) {
        skipOrAdvanceComic();
    } else if (state == ModeSelect) {
        return;
    } else if (state == Playing) {
        birdVel = FLAP_VEL;
    } else if (state == GameOver) {
        beginPlay(hardMode, trollMode);
    }
}

void MainWindow::onShoot()
{
    // Fire the stocked feather blade. Only one blade may be in flight.
    if (state != Playing || trollPauseMs > 0) return;
    if (!hasBlade || !blades.empty()) return;

    FeatherBlade b;
    b.x = (float)(birdX + currentBirdW());
    b.y = birdY + currentBirdH() / 2.0f;
    blades.push_back(b);
    hasBlade = false;
    // The next blade comes 10 points after the score at the moment of firing.
    nextBladeScore = score + BLADE_SCORE_INTERVAL;
}

void MainWindow::tryAwardBlade()
{
    // At most one blade in stock; none while one is flying.
    if (hasBlade || !blades.empty()) return;
    if (score >= nextBladeScore)
        hasBlade = true;
}

void MainWindow::resetGame()
{
    // Reset the gameplay state for a fresh run.
    // This keeps the bird, pipes, score and animation states consistent.
    updateGridSize();
    applyDifficulty();
    birdY = (gridH - groundH) / 2.0f;
    birdVel = 0;
    pipes.clear();
    fallingLetters.clear();
    cactusBalls.clear();
    blades.clear();
    hasBlade = true;
    nextBladeScore = BLADE_SCORE_INTERVAL;
    cannonHits = 0;
    cannonAlive = true;
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
    birdScale = 1;
    birdScaleMs = 0;
    distSinceLastPipe = pipeSpacing;
    score = 0;
    groundOffset = 0;
    bobTimer = 0;
    ui->scoreLabel->setText("Score: 0");
}

void MainWindow::updateGame()
{
    // Main game loop tick. This updates physics, scoring, enemies and screen animation.
    updateGridSize();

    if (state == ComicIntro) {
        comicMs += TICK_MS;
        if (comicMs >= COMIC_PANEL_MS) {
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
        if (groundOffset >= 4.0f) groundOffset -= 4.0f;
    } else if (state == Playing) {
        if (trollMode && trollPauseMs > 0) {
            trollPauseMs -= TICK_MS;
            if (trollPauseMs < 0) trollPauseMs = 0;
            if (trollPauseMs == 0)
                ui->statusLabel->setText("TROLL MODE");
            renderFrame();
            return;
        }

        birdVel += GRAVITY;
        if (birdVel > MAX_FALL) birdVel = MAX_FALL;
        birdY += birdVel;

        if (trollMode) {
            updateTroll();
            updateHeadingLetters();
            birdX += (int)std::round(windX);
            if (birdX < 1) birdX = 1;
            int maxX = gridW - currentBirdW() - 1;
            if (birdX > maxX) birdX = qMax(1, maxX);
        }

        if (birdY < 0) {
            birdY = 0;
            birdVel = 0;
        }

        for (auto &p : pipes) {
            p.x -= pipeSpeed;
        }

        while (!pipes.empty() && pipes.front().x + pipeWidth < 0) {
            pipes.erase(pipes.begin());
        }

        distSinceLastPipe += pipeSpeed;
        if (distSinceLastPipe >= pipeSpacing) {
            spawnPipe();
            distSinceLastPipe = 0;
        }

        for (auto &p : pipes) {
            if (!p.scored && (p.x + pipeWidth) < birdX) {
                p.scored = true;
                score++;
                ui->scoreLabel->setText("Score: " + QString::number(score));
                updateHardSpeed();
                tryAwardBlade();
            }
        }

        groundOffset += pipeSpeed;
        if (groundOffset >= 4.0f) groundOffset -= 4.0f;

        if (hardMode) {
            updateEaters();
            updateHardCannon();
            hardPlayMs += TICK_MS;
            if (hardPlayMs >= FLASH_EVERY_MS) {
                hardPlayMs -= FLASH_EVERY_MS;
                flashMsLeft = FLASH_DURATION_MS;
            }
            if (flashMsLeft > 0)
                flashMsLeft -= TICK_MS;
        }

        updateBlades();

        if (checkCollision()) {
            state = GameOver;
            if (score > highScore) {
                highScore = score;
                ui->highScoreLabel->setText("Best: " + QString::number(highScore));
            }
            ui->restartButton->show();
            ui->statusLabel->setText("GAME OVER!\nSPACE to retry\nRestart to choose mode");
        }
    }

    renderFrame();
}

void MainWindow::spawnPipe()
{
    // Create a new obstacle column. The gap position changes randomly,
    // so each pipe forces a different path through the level.
    Pipe p;
    p.x = (float)gridW;
    int playableH = gridH - groundH;
    int minGapTop = 3;
    int maxGapTop = playableH - pipeGap - 3;
    if (maxGapTop < minGapTop) maxGapTop = minGapTop;
    p.gapTop = QRandomGenerator::global()->bounded(minGapTop, maxGapTop + 1);
    p.gapHeight = pipeGap;
    p.scored = false;
    p.hasEater = hardMode;
    // Troll pipes can be harmless decoys that turn solid near the bird.
    p.fakePipe = trollMode && (QRandomGenerator::global()->bounded(100) < 28);
    p.fakeActivated = false;
    p.gapShifted = false;
    p.originalGapTop = p.gapTop;
    // Some Troll pipes can lunge forward during a random event.
    p.windAffected = trollMode && (QRandomGenerator::global()->bounded(100) < 35);
    p.eaterFromTop = QRandomGenerator::global()->bounded(2) == 0;
    p.eaterTriggered = false;
    p.eaterOut = 0;
    p.eaterMax = 5.0f;
    if (p.eaterMax > p.gapHeight - 2)
        p.eaterMax = (float)qMax(2, p.gapHeight - 2);
    pipes.push_back(p);
}

void MainWindow::updateEaters()
{
    for (auto &p : pipes) {
        if (!p.hasEater) continue;
        float distAhead = p.x - (float)birdX;
        if (!p.eaterTriggered && distAhead < 16.0f && distAhead > 0.0f) {
            p.eaterTriggered = true;
        }
        if (p.eaterTriggered) {
            if (p.x + pipeWidth > birdX - 4)
                p.eaterOut += 0.45f;
            else
                p.eaterOut -= 0.35f;
            if (p.eaterOut > p.eaterMax) p.eaterOut = p.eaterMax;
            if (p.eaterOut < 0) p.eaterOut = 0;
        }
    }
}

void MainWindow::updateHardCannon()
{
    if (!hardMode) return;

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
        float birdCenterX = birdX + currentBirdW() / 2.0f;
        float birdCenterY = birdY + currentBirdH() / 2.0f;
        float dx = birdCenterX - cannonCenterX;
        float dy = birdCenterY - cannonCenterY;
        float distance = std::sqrt(dx * dx + dy * dy);
        if (distance > 0.0f) {
            cannonAimX = dx / distance;
            cannonAimY = dy / distance;
        }

        CactusBall ball;
        float muzzleDistance = CANNON_W / 2.0f + 0.8f;
        ball.x = cannonCenterX + cannonAimX * muzzleDistance;
        ball.y = cannonCenterY + cannonAimY * muzzleDistance;
        ball.vx = cannonAimX * 1.35f;
        ball.vy = cannonAimY * 1.35f;
        cactusBalls.push_back(ball);
        cannonFireMs = QRandomGenerator::global()->bounded(1700, 2700);
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
    int h = qMin(FEATHER_BLADE_GAP, playableH);
    int top = qBound(0, (int)std::floor(y) - h / 2, playableH - h);
    p.extraGaps.push_back({top, top + h});
}

void MainWindow::updateBlades()
{
    if (blades.empty()) return;

    const int playableH = gridH - groundH;
    std::vector<FeatherBlade> alive;

    for (auto &b : blades) {
        const float oldFront = b.x;
        const float newFront = b.x + FEATHER_BLADE_SPEED;
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
            if (++cannonHits >= CANNON_HITS_TO_KILL)
                cannonAlive = false;
            continue;
        }
        if (kind == Log) {
            cutPipeAt(pipes[pipeIdx], b.y);
            continue;
        }

        if (row < 0 || row >= playableH) continue; // left the playfield
        b.x = newFront;
        if (b.x - FEATHER_BLADE_LEN > gridW) continue; // flew off screen
        alive.push_back(b);
    }

    blades.swap(alive);
}

int MainWindow::currentBirdW() const
{
    return BIRD_W * birdScale;
}

int MainWindow::currentBirdH() const
{
    return BIRD_H * birdScale;
}

void MainWindow::updateTroll()
{
    if (!trollMode) return;

    if (birdScaleMs > 0) {
        birdScaleMs -= TICK_MS;
        if (birdScaleMs <= 0) {
            birdScaleMs = 0;
            birdScale = 1;
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
    trollEventMs = QRandomGenerator::global()->bounded(2200, 5200);

    int event = QRandomGenerator::global()->bounded(4);

    // Make a nearby fake pipe real at the last moment.
    for (auto &p : pipes) {
        if (p.fakePipe && QRandomGenerator::global()->bounded(1) == 0 && !p.fakeActivated && p.x > birdX + 5 && p.x < birdX + 35) {
            p.fakeActivated = true;
            break;
        }
    }

    if (event == 0) {
        // Sudden wind burst.
        windX = QRandomGenerator::global()->bounded(2) ? 7.6f : -7.6f;
        windAnimMs = 0;
    } else if (event == 1) {
        // Fake gap: the visible opening suddenly shifts/closes briefly.
        if (!pipes.empty()) {
            int idx = QRandomGenerator::global()->bounded((int)pipes.size());
            Pipe &p = pipes[idx];
            if (p.x > birdX - 10 && p.x < birdX + 45) {
                int shift = QRandomGenerator::global()->bounded(2) ? 6 : -6;
                fakeGapMs = 650;
                p.originalGapTop = p.gapTop;
                p.gapShifted = true;
                p.gapTop += shift;
                int playableH = gridH - groundH;
                p.gapTop = qBound(2, p.gapTop, playableH - p.gapHeight - 2);
            }
        }
    }  else if (event == 2) {
        // Sudden horizontal pipe lunge.
        for (auto &p : pipes) {
            if (p.windAffected && p.x > birdX && p.x < birdX + 50) {
                p.x -= QRandomGenerator::global()->bounded(4, 10);
                break;
            }
        }
    } else {
        // Randomly make the bird huge or tiny for a short stretch.
        birdScale = QRandomGenerator::global()->bounded(2) ? 2 : 1;
        if (birdScale == 1 && QRandomGenerator::global()->bounded(3) == 0)
            birdScale = 3;
        birdScaleMs = QRandomGenerator::global()->bounded(1800, 4200);
    }

    // Very occasional fake GAME OVER prank.
    if (QRandomGenerator::global()->bounded(100) < 18 && score >= 3) {
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
            L.velY += GRAVITY * 0.7f;
            if (L.velY > MAX_FALL) L.velY = MAX_FALL;
            L.y += L.velY;
            if (L.y + LETTER_H >= playableH) {
                L.y = (float)(playableH - LETTER_H);
                L.velY = 0;
                L.grounded = true;
            }
        } else {
            L.x -= pipeSpeed;
        }
    }

    fallingLetters.erase(
        std::remove_if(fallingLetters.begin(), fallingLetters.end(),
                       [](const FallingLetter &L) { return L.x + LETTER_W < 0; }),
        fallingLetters.end());
}

bool MainWindow::letterHitsBird(const FallingLetter &L) const
{
    int by = (int)birdY;
    int lx = (int)L.x;
    int ly = (int)L.y;
    return birdX + currentBirdW() > lx && birdX < lx + LETTER_W &&
           by + currentBirdH() > ly && by < ly + LETTER_H;
}

bool MainWindow::eaterHitsBird(const Pipe &p) const
{
    if (!p.hasEater || p.eaterOut < 1.0f) return false;
    int by = (int)birdY;
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
    return birdX + currentBirdW() > hx && birdX < hx + headW &&
           by + currentBirdH() > hy && by < hy + hh;
}

bool MainWindow::checkCollision()
{
    int by = (int)birdY;
    int bw = currentBirdW();
    int bh = currentBirdH();
    int playableH = gridH - groundH;

    if (by + bh > playableH) return true;
    if (by < 0) return true;

    for (const auto &p : pipes) {
        int px = (int)p.x;
        bool solidPipe = !trollMode || !p.fakePipe || p.fakeActivated;
        if (solidPipe && birdX + bw > px && birdX < px + pipeWidth) {
            for (int r = by; r < by + bh; r++) {
                if (pipeRowSolid(p, r))
                    return true;
            }
        }
        if (hardMode && eaterHitsBird(p)) return true;
    }

    if (hardMode) {
        for (const auto &ball : cactusBalls) {
            if (ball.x + CACTUS_BALL_RADIUS > birdX &&
                ball.x - CACTUS_BALL_RADIUS < birdX + bw &&
                ball.y + CACTUS_BALL_RADIUS > by &&
                ball.y - CACTUS_BALL_RADIUS < by + bh)
                return true;
        }
    }

    if (trollMode) {
        for (const auto &L : fallingLetters) {
            if (letterHitsBird(L)) return true;
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
    for (int row = 0; row < gridH; row++) {
        int g = 28 + (row * 90) / gridH;
        int r = 8 + (row * 18) / gridH;
        int b = 12 + (row * 22) / gridH;
        for (int col = 0; col < gridW; col++) {
            fillCell(img, col, row, r, g, b);
        }
    }
}

void MainWindow::drawTrees(QImage &img)
{
    int playableH = gridH - groundH;
    int bases[] = {6, 18, 31, 48, 62, 78, 91};
    int heights[] = {18, 24, 16, 28, 20, 22, 15};
    int go = (int)(groundOffset * 0.4f);
    for (int i = 0; i < 7; i++) {
        int col = (bases[i] - go);
        while (col < 0) col += 100;
        col %= (gridW > 10 ? gridW : 100);
        int h = heights[i];
        if (h > playableH - 4) h = playableH - 4;
        int top = playableH - h;
        for (int row = top; row < playableH; row++) {
            fillCell(img, col, row, 20, 55, 22);
            fillCell(img, col + 1, row, 16, 45, 18);
        }
        for (int dy = 0; dy < 5; dy++) {
            for (int dx = -2; dx <= 3; dx++) {
                fillCell(img, col + dx, top + dy, 18, 70, 24);
            }
        }
    }
}

void MainWindow::drawBird(QImage &img)
{
    int by = (int)birdY;
    int bw = currentBirdW();
    int bh = currentBirdH();
    for (int dy = 0; dy < bh; dy++) {
        for (int dx = 0; dx < bw; dx++)
            fillCell(img, birdX + dx, by + dy, 255, 220, 0);
    }
    fillCell(img, birdX + bw - 1, by, 255, 255, 255);
    {
        int ex = (birdX + bw - 1) * CELL + CELL - 2;
        int ey = by * CELL + 1;
        if (ex >= 0 && ex < img.width() && ey >= 0 && ey < img.height()) {
            img.setPixel(ex, ey, qRgb(0, 0, 0));
            if (ex + 1 < img.width()) img.setPixel(ex + 1, ey, qRgb(0, 0, 0));
            if (ey + 1 < img.height()) img.setPixel(ex, ey + 1, qRgb(0, 0, 0));
        }
    }
    fillCell(img, birdX + bw, by + bh - 1, 255, 140, 0);
}

void MainWindow::drawPipes(QImage &img)
{
    int playableH = gridH - groundH;
    for (const auto &p : pipes) {
        int px = (int)p.x;
        for (int dx = 0; dx < pipeWidth; dx++) {
            int col = px + dx;
            if (col < 0 || col >= gridW) continue;

            bool edge = (dx == 0 || dx == pipeWidth - 1);
            int wr = edge ? 90 : 158;
            int wg = edge ? 58 : 112;
            int wb = edge ? 28 : 58;
            if (trollMode && p.fakePipe && !p.fakeActivated) {
                wr = 110; wg = 130; wb = 95;
            }

            for (int row = 0; row < playableH; row++) {
                if (!pipeRowSolid(p, row)) continue;
                bool ring = (row % 4 == 0);
                fillCell(img, col, row,
                         ring ? wr - 30 : wr,
                         ring ? wg - 20 : wg,
                         ring ? wb - 10 : wb);
            }

            // Edge caps at the original gap and at every blade-cut hole.
            auto capGap = [&](int top, int bottom) {
                if (dx == 0 || dx == pipeWidth - 1) {
                    int cx = dx == 0 ? col - 1 : col + 1;
                    if (top - 1 >= 0 && pipeRowSolid(p, top - 1))
                        fillCell(img, cx, top - 1, 120, 80, 40);
                    if (bottom < playableH && pipeRowSolid(p, bottom))
                        fillCell(img, cx, bottom, 120, 80, 40);
                }
            };
            capGap(p.gapTop, p.gapTop + p.gapHeight);
            for (const auto &g : p.extraGaps)
                capGap(g.first, g.second);
        }
    }
}

void MainWindow::drawEaters(QImage &img)
{
    for (const auto &p : pipes) {
        if (!p.hasEater || p.eaterOut < 0.5f) continue;
        int px = (int)p.x;
        int headW = qMax(3, pipeWidth - 2);
        int hx = px + (pipeWidth - headW) / 2;
        int hh = (int)p.eaterOut;
        if (hh < 1) continue;
        int hy = p.eaterFromTop ? p.gapTop : (p.gapTop + p.gapHeight - hh);
        for (int dy = 0; dy < hh; dy++) {
            for (int dx = 0; dx < headW; dx++) {
                int rr = 40, gg = 150, bb = 50;
                if (dy == 0 || dy == hh - 1 || dx == 0 || dx == headW - 1) {
                    rr = 180; gg = 40; bb = 40;
                }
                if ((dx + dy) % 3 == 0 && dy > 0 && dy < hh - 1) {
                    rr = 220; gg = 220; bb = 220;
                }
                fillCell(img, hx + dx, hy + dy, rr, gg, bb);
            }
        }
        int mouthY = p.eaterFromTop ? (hy + hh - 1) : hy;
        for (int dx = 1; dx < headW - 1; dx++)
            fillCell(img, hx + dx, mouthY, 40, 20, 20);
    }
}

void MainWindow::drawCannon(QImage &img)
{
    int baseX = qRound(cannonX);
    int baseY = qRound(cannonY);
    fillCell(img, baseX + 3, baseY + 5, 35, 135, 48);
    fillCell(img, baseX + 3, baseY + 6, 35, 135, 48);
    fillCell(img, baseX + 3, baseY + 7, 35, 135, 48);
    fillCell(img, baseX + 1, baseY + 6, 50, 175, 58);
    fillCell(img, baseX + 2, baseY + 5, 50, 175, 58);
    fillCell(img, baseX + 5, baseY + 6, 50, 175, 58);
    fillCell(img, baseX + 5, baseY + 5, 50, 175, 58);

    float centerX = (cannonX + CANNON_W / 2.0f) * CELL;
    float centerY = (cannonY + CANNON_H / 2.0f) * CELL;
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(32, 85, 38), 7, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(centerX + cannonAimX * 9.0f,
                             centerY + cannonAimY * 9.0f),
                     QPointF(centerX + cannonAimX * 24.0f,
                             centerY + cannonAimY * 24.0f));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(74, 170, 61));
    painter.drawEllipse(QPointF(centerX, centerY), 11.0, 11.0);
    painter.setBrush(QColor(30, 65, 29));
    painter.drawEllipse(QPointF(centerX + cannonAimX * 9.0f,
                                centerY + cannonAimY * 9.0f), 3.0, 3.0);
    painter.end();
}

void MainWindow::drawCactusBalls(QImage &img)
{
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const auto &ball : cactusBalls) {
        QPointF center(ball.x * CELL, ball.y * CELL);
        painter.setPen(QPen(QColor(32, 100, 37), 2));
        for (int direction = 0; direction < 8; direction++) {
            float angle = direction * 0.785398f;
            QPointF inner(center.x() + std::cos(angle) * 5.0f,
                          center.y() + std::sin(angle) * 5.0f);
            QPointF outer(center.x() + std::cos(angle) * 10.0f,
                          center.y() + std::sin(angle) * 10.0f);
            painter.drawLine(inner, outer);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(90, 190, 65));
        painter.drawEllipse(center, 7.0, 7.0);
    }
    painter.end();
}

void MainWindow::drawBlades(QImage &img)
{
    if (blades.empty()) return;
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto &b : blades) {
        float tipX = b.x * CELL;
        float cy = (b.y + 0.5f) * CELL;
        float len = FEATHER_BLADE_LEN * CELL;
        QPolygonF feather;
        feather << QPointF(tipX, cy)
                << QPointF(tipX - len * 0.7f, cy - 5)
                << QPointF(tipX - len, cy)
                << QPointF(tipX - len * 0.7f, cy + 5);
        painter.setPen(QPen(QColor(120, 200, 255), 1));
        painter.setBrush(QColor(235, 248, 255));
        painter.drawPolygon(feather);
    }
    painter.end();
}

void MainWindow::drawBladeHud(QImage &img)
{
    int cx = (gridW * CELL) / 2;
    int scoreY = hardMode ? 58 : 40;
    int y = scoreY + 24;
    if (hasBlade) {
        drawText(img, "BLADE READY [ALT]", cx, y, 120, 220, 255, 11);
    } else if (!blades.empty()) {
        drawText(img, "BLADE", cx, y, 235, 248, 255, 11);
    } else {
        drawText(img, "BLADE at " + QString::number(nextBladeScore), cx, y, 150, 150, 150, 11);
    }
    if (hardMode && cannonAlive && cannonHits > 0) {
        drawText(img, "CANNON " + QString::number(CANNON_HITS_TO_KILL - cannonHits) + " HP",
                 cx, y + 20, 255, 140, 140, 10);
    }
}

void MainWindow::drawGround(QImage &img)
{
    int groundTop = gridH - groundH;
    int go = (int)groundOffset;
    for (int row = groundTop; row < gridH; row++) {
        for (int col = 0; col < gridW; col++) {
            if (row == groundTop) {
                if ((col + go) % 2 == 0)
                    fillCell(img, col, row, 70, 160, 55);
                else
                    fillCell(img, col, row, 45, 120, 40);
            } else {
                if ((col + row + go) % 3 == 0)
                    fillCell(img, col, row, 180, 120, 60);
                else
                    fillCell(img, col, row, 160, 100, 40);
            }
        }
    }
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
    QPainter painter(&img);
    painter.fillRect(img.rect(), Qt::black);
    if (comicPanel >= 0 && comicPanel < (int)comicPanels.size() && !comicPanels[comicPanel].isNull()) {
        QPixmap scaled = comicPanels[comicPanel].scaled(img.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (img.width() - scaled.width()) / 2;
        int y = (img.height() - scaled.height()) / 2;
        painter.drawPixmap(x, y, scaled);
    }
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
    drawText(img, "Choose Easy / Hard / Troll", cx, cy + 50, 210, 230, 200, 14);

    float bobOffset = std::sin(bobTimer) * 3.0f;
    int by = (int)(((gridH - groundH) / 2.0f) + bobOffset);
    int bx = gridW / 2 - 1;
    for (int dy = 0; dy < BIRD_H; dy++) {
        for (int dx = 0; dx < BIRD_W; dx++) {
            fillCell(img, bx + dx, by + dy, 255, 220, 0);
        }
    }
    fillCell(img, bx + BIRD_W - 1, by, 255, 255, 255);
    fillCell(img, bx + BIRD_W, by + BIRD_H - 1, 255, 140, 0);
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

    drawText(img, "GAME OVER", cx, cy, 255, 60, 60, 26);
    drawText(img, "Score: " + QString::number(score), cx, cy + 50, 255, 255, 255, 18);
    drawText(img, "Best: " + QString::number(highScore), cx, cy + 85, 255, 220, 0, 16);
    drawText(img, "SPACE: retry", cx, cy + 130, 200, 200, 200, 13);
    drawText(img, "Restart: choose mode", cx, cy + 155, 200, 200, 200, 13);
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
        drawBird(img);
        drawBlades(img);
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
        int cx = (gridW * CELL) / 2;
        int scoreY = hardMode ? 58 : 40;
        drawText(img, QString::number(score), cx, scoreY, 255, 255, 255, 24);
        drawBladeHud(img);
        drawFlash(img);
    }

    ui->frame->setPixmap(QPixmap::fromImage(img));
}
