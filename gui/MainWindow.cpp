#include "MainWindow.h"

#include <QApplication>
#include <QAudioOutput>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QLinearGradient>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRectF>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QSoundEffect>
#include <QThread>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <cstring>

#include "board.h"

// ============================================================================
// 棋盘几何：所有渲染和“像素 <-> 棋盘坐标”换算的唯一数据源
// 棋盘 9 列 10 行交叉点；棋子摆在交叉点上。
// ============================================================================
namespace {

constexpr int kRows = 10;
constexpr int kCols = 9;
constexpr qreal kMargin = 30.0;   // 棋盘外沿留白（炮位/兵位小十字需要空间）

struct Geo {
    qreal ox;     // 左上角交叉点 x
    qreal oy;     // 左上角交叉点 y
    qreal cell;   // 格距
};

Geo makeGeo(const QSize &s)
{
    Geo g;
    g.cell = qMin((s.width()  - 2.0 * kMargin) / 8.0,
                  (s.height() - 2.0 * kMargin) / 9.0);
    g.ox = (s.width()  - 8.0 * g.cell) / 2.0;
    g.oy = (s.height() - 9.0 * g.cell) / 2.0;
    return g;
}

QPointF crossPt(const Geo &g, int row, int col)
{
    return QPointF(g.ox + col * g.cell, g.oy + row * g.cell);
}

// 像素坐标 -> 最近的交叉点（超出半个格距则判定未点中）
bool hitCross(const Geo &g, const QPoint &pos, int &row, int &col)
{
    const qreal fx = (pos.x() - g.ox) / g.cell;
    const qreal fy = (pos.y() - g.oy) / g.cell;
    col = int(qRound(fx));
    row = int(qRound(fy));
    if (row < 0 || row >= kRows || col < 0 || col >= kCols)
        return false;
    return QLineF(QPointF(pos), crossPt(g, row, col)).length() <= g.cell * 0.5;
}

// 在棋盘上找指定棋子（K/k），找不到返回 false
bool findPiece(int code, int &row, int &col)
{
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            if (coreBoard[r][c] == code) {
                row = r;
                col = c;
                return true;
            }
    return false;
}

// 棋子编码（大写红/小写黑）-> 传统棋子字
QString pieceText(int ch)
{
    switch (ch) {
    case 'K': return QStringLiteral("帅");
    case 'A': return QStringLiteral("仕");
    case 'B': return QStringLiteral("相");
    case 'N': return QStringLiteral("马");
    case 'R': return QStringLiteral("车");
    case 'P': return QStringLiteral("炮");
    case 'S': return QStringLiteral("兵");
    case 'k': return QStringLiteral("将");
    case 'a': return QStringLiteral("士");
    case 'b': return QStringLiteral("象");
    case 'n': return QStringLiteral("馬");
    case 'r': return QStringLiteral("車");
    case 'p': return QStringLiteral("砲");
    case 's': return QStringLiteral("卒");
    default:  return QString();
    }
}

const QColor kLineColor(92, 58, 26);      // 棋盘线深棕
const QColor kRiverColor(120, 72, 30);    // 楚河汉界字色
const QColor kRedText(200, 38, 34);       // 红方棋子字色
const QColor kBlackText(32, 28, 26);      // 黑方棋子字色
const QColor kSelectColor(232, 162, 30);  // 选中金圈
const QColor kHintColor(46, 125, 50);     // 可走点绿色
const QColor kCaptureColor(200, 60, 50);  // 可吃子红圈
const QColor kLastColor(232, 162, 30, 60);// 上一步标记
const QColor kCheckColor(214, 34, 30);    // 被将军的将帅警示红

// 炮位/兵位旁的小直角标记
void drawCornerTicks(QPainter &p, const Geo &g, int row, int col)
{
    const QPointF c = crossPt(g, row, col);
    const qreal gap = g.cell * 0.16;
    const qreal len = g.cell * 0.13;
    // dirX/dirY：标记允许伸出的方向（靠边的点只画朝棋盘内侧的部分）
    for (int ix = -1; ix <= 1; ix += 2) {
        if ((ix < 0 && col == 0) || (ix > 0 && col == kCols - 1))
            continue;
        for (int iy = -1; iy <= 1; iy += 2) {
            if ((iy < 0 && row == 0) || (iy > 0 && row == kRows - 1))
                continue;
            const qreal x0 = c.x() + ix * gap;
            const qreal y0 = c.y() + iy * gap;
            QPainterPath path;
            path.moveTo(x0 + ix * len, y0);
            path.lineTo(x0, y0);
            path.lineTo(x0, y0 + iy * len);
            p.drawPath(path);
        }
    }
}

} // namespace

// ============================================================================
// BoardView
// ============================================================================
BoardView::BoardView(MainWindow *win, QWidget *parent)
    : QWidget(parent), m_win(win)
{
    setMinimumSize(480, 540);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void BoardView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    m_win->renderBoard(p, size());
}

void BoardView::mousePressEvent(QMouseEvent *event)
{
    m_win->handleBoardPress(event->pos(), size(),
                            event->button() == Qt::RightButton);
}

// ============================================================================
// MainWindow
// ============================================================================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("中国象棋 - candyboard"));

    // 顶部按钮行 + 棋盘
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);

    auto *btnRow = new QHBoxLayout();
    m_restartBtn = new QPushButton(QStringLiteral("重新开局"), central);
    m_undoBtn = new QPushButton(QStringLiteral("悔棋"), central);
    m_resignBtn = new QPushButton(QStringLiteral("投降"), central);
    btnRow->addWidget(m_restartBtn);
    btnRow->addWidget(m_undoBtn);
    btnRow->addWidget(m_resignBtn);
    btnRow->addSpacing(16);
    btnRow->addWidget(new QLabel(QStringLiteral("AI 难度："), central));
    m_levelCombo = new QComboBox(central);   // 必须先 new 出下拉框，才能往里 addItem
    m_levelCombo->addItem(QStringLiteral("笨糖果（10 秒）"), 10000);
    m_levelCombo->addItem(QStringLiteral("一般糖果（15 秒）"), 15000);
    m_levelCombo->addItem(QStringLiteral("聪明糖果（20 秒）"), 20000);
    m_levelCombo->setCurrentIndex(0);   // 默认“笨糖果”
    btnRow->addWidget(m_levelCombo);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 第二行：背景 / 音乐 / 音量 / 先后手
    auto *optRow = new QHBoxLayout();
    optRow->addWidget(new QLabel(QStringLiteral("背景："), central));
    m_bgCombo = new QComboBox(central);
    m_bgCombo->addItem(QStringLiteral("原皮"));
    m_bgCombo->addItem(QStringLiteral("蓝发少女"));
    m_bgCombo->addItem(QStringLiteral("龙族"));
    m_bgCombo->addItem(QStringLiteral("自定义…"));
    optRow->addWidget(m_bgCombo);

    optRow->addSpacing(12);
    optRow->addWidget(new QLabel(QStringLiteral("音乐："), central));
    m_musicCombo = new QComboBox(central);
    m_musicCombo->addItem(QStringLiteral("无声"));
    m_musicCombo->addItem(QStringLiteral("The Cure+That Girl+One Last Time"));
    m_musicCombo->addItem(QStringLiteral("阳光下的星星"));
    m_musicCombo->addItem(QStringLiteral("Take Me Hand"));
    m_musicCombo->addItem(QStringLiteral("自定义…"));
    optRow->addWidget(m_musicCombo);

    optRow->addSpacing(12);
    optRow->addWidget(new QLabel(QStringLiteral("音量："), central));
    m_volSlider = new QSlider(Qt::Horizontal, central);
    m_volSlider->setRange(0, 100);
    m_volSlider->setValue(50);
    m_volSlider->setFixedWidth(110);
    optRow->addWidget(m_volSlider);

    optRow->addSpacing(12);
    optRow->addWidget(new QLabel(QStringLiteral("执子："), central));
    m_sideCombo = new QComboBox(central);
    m_sideCombo->addItem(QStringLiteral("先手（执红）"));
    m_sideCombo->addItem(QStringLiteral("后手（执黑）"));
    optRow->addWidget(m_sideCombo);
    optRow->addStretch();
    root->addLayout(optRow);

    // 背景音乐播放器（循环播放）
    m_audioOut = new QAudioOutput(this);
    m_audioOut->setVolume(0.5f);
    m_musicPlayer = new QMediaPlayer(this);
    m_musicPlayer->setAudioOutput(m_audioOut);
    m_musicPlayer->setLoops(QMediaPlayer::Infinite);

    connect(m_bgCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onBgChanged);
    connect(m_musicCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onMusicChanged);
    connect(m_sideCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSideChanged);
    // 音量滑条 0-100 -> 0.0-1.0
    connect(m_volSlider, &QSlider::valueChanged, this, [this](int v) {
        m_audioOut->setVolume(v / 100.0f);
    });

    m_view = new BoardView(this, central);

    // 右侧：对面虚拟形象面板（伪3D卡片）
    initAvatar();
    auto *contentRow = new QHBoxLayout();
    contentRow->setSpacing(8);
    contentRow->addWidget(m_view, 1);

    auto *avatarFrame = new QFrame(central);
    avatarFrame->setObjectName(QStringLiteral("avatarFrame"));
    avatarFrame->setStyleSheet(QStringLiteral(
        "QFrame#avatarFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0.4, y2:1,"
        "    stop:0 #fffdf4, stop:0.55 #ffe9b8, stop:1 #e7c07f);"
        "  border: 2px solid #8a5a2a; border-radius: 14px;"
        "}"));
    // 投影：让卡片浮起来，营造伪3D感
    auto *shadow = new QGraphicsDropShadowEffect(avatarFrame);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(60, 36, 10, 110));
    avatarFrame->setGraphicsEffect(shadow);

    auto *avatarCol = new QVBoxLayout(avatarFrame);
    avatarCol->setContentsMargins(12, 12, 12, 12);
    avatarCol->setSpacing(6);
    auto *nameLabel = new QLabel(QStringLiteral("奶娃"), avatarFrame);
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    avatarCol->addWidget(nameLabel);
    avatarCol->addWidget(m_avatarImg, 0, Qt::AlignCenter);
    avatarCol->addWidget(m_avatarMood);
    avatarCol->addStretch();
    avatarFrame->setFixedWidth(200);
    contentRow->addWidget(avatarFrame);
    root->addLayout(contentRow, 1);

    setCentralWidget(central);
    resize(920, 700);   // 加宽以容纳第二行选项（背景/音乐/音量/执子）

    connect(m_restartBtn, &QPushButton::clicked, this, &MainWindow::restartGame);
    connect(m_undoBtn, &QPushButton::clicked, this, &MainWindow::undoMove);
    connect(m_resignBtn, &QPushButton::clicked, this, &MainWindow::resignGame);

    // 将帅被将军时的呼吸动画
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(80);
    connect(m_animTimer, &QTimer::timeout, this, [this] {
        m_animPhase += 0.18;
        m_view->update();
    });

    // AI 思考计时显示（AI 在后台线程，界面持续响应）
    m_thinkTimer = new QTimer(this);
    m_thinkTimer->setInterval(100);
    connect(m_thinkTimer, &QTimer::timeout, this, &MainWindow::onAiTick);
    m_thinkClock = new QElapsedTimer();

    statusBar()->showMessage(QStringLiteral("红方走棋（你执红先行）"));

    initSounds();   // 必须在开局前预加载，否则第一声会因异步加载而哑火
    loadCustomAssets();   // 恢复上次用过的自定义背景/音乐
    initBoard();
    resetGameHash();
    resetKillerAndHistory();
    setAvatar(AvatarState::Idle);
    refreshButtons();
}

MainWindow::~MainWindow()
{
    // 关窗口时 AI 还在算就等它结束，避免线程访问已销毁对象
    if (m_aiThread && m_aiThread->isRunning())
        m_aiThread->wait();
    delete m_thinkClock;
}

// 预加载音效：wav 已编进 qrc，程序启动时一次性载入内存，点击即播无延迟
void MainWindow::initSounds()
{
    auto mk = [this](const QString &file, float vol) {
        auto *s = new QSoundEffect(this);
        s->setSource(QUrl(QStringLiteral("qrc:/sounds/") + file));
        s->setVolume(vol);
        return s;
    };
    m_sndSelect     = mk(QStringLiteral("select.wav"),      0.5f);
    m_sndMove       = mk(QStringLiteral("move.wav"),        0.6f);
    m_sndCapture    = mk(QStringLiteral("capture.wav"),     0.6f);   // 黑方/对面吃子（视频提取）
    m_sndCaptureRed = mk(QStringLiteral("capture_red.wav"), 0.6f);   // 红方/我方吃子（原版合成）
    m_sndCheck      = mk(QStringLiteral("check.wav"),       0.6f);   // 黑方/对面将军（视频提取）
    m_sndCheckRed   = mk(QStringLiteral("check_red.wav"),   0.6f);   // 红方/我方将军（原版合成）
    m_sndWin        = mk(QStringLiteral("win.wav"),         0.6f);
    m_sndLose       = mk(QStringLiteral("lose.wav"),        0.6f);   // 黑方胜利/投降大笑（视频提取）
}

// 初始化对面虚拟形象：图片标签 + 心情文字 + gif 动图 + 反应定时器
void MainWindow::initAvatar()
{
    m_avatarImg = new QLabel(this);
    m_avatarImg->setFixedSize(160, 160);
    m_avatarImg->setAlignment(Qt::AlignCenter);

    m_avatarMood = new QLabel(this);
    m_avatarMood->setAlignment(Qt::AlignCenter);
    m_avatarMood->setWordWrap(true);

    // 被吃子后的 gif 动作：用户提供文件并加入 avatar.qrc 后自动生效；
    // 资源缺失时 isValid() 为假，Action 状态退化为保持愤怒脸
    m_avatarMovie = new QMovie(QStringLiteral(":/avatar/action.gif"), QByteArray(), this);
    m_avatarMovie->setCacheMode(QMovie::CacheAll);

    m_avatarTimer = new QTimer(this);
    m_avatarTimer->setSingleShot(true);
    connect(m_avatarTimer, &QTimer::timeout, this, &MainWindow::onAvatarTimer);
}

// 切换形象：静态图直接换 pixmap；Action 状态播 gif
// 音乐开启时锁定为听歌脸，拒绝任何切换（但 Listening 自己放行）
void MainWindow::setAvatar(AvatarState s)
{
    if (m_musicOn && s != AvatarState::Listening)
        s = AvatarState::Listening;
    m_avatarState = s;

    if (s == AvatarState::Action && m_avatarMovie->isValid()) {
        m_avatarImg->setMovie(m_avatarMovie);
        m_avatarMovie->start();
        m_avatarMood->setText(QStringLiteral("看我反击！"));
        return;
    }

    QString file;
    QString mood;
    switch (s) {
    case AvatarState::Idle:
        file = QStringLiteral("idle.jpg");
        mood = QStringLiteral("我只是一只小奶龙");
        break;
    case AvatarState::Selected:
        file = QStringLiteral("selected.jpg");
        mood = QStringLiteral("绷不住了");
        break;
    case AvatarState::Thinking:
        file = QStringLiteral("thinking.jpg");
        mood = QStringLiteral("思考中…");
        break;
    case AvatarState::Angry:
        file = QStringLiteral("angry.jpg");
        mood = QStringLiteral("敢吃我的子？！");
        break;
    case AvatarState::Action:   // gif 缺失：保持愤怒脸，文字照换
        file = QStringLiteral("angry.jpg");
        mood = QStringLiteral("看我反击！");
        break;
    case AvatarState::GoodMove:
        file = QStringLiteral("good.jpg");
        mood = QStringLiteral("这一步不错！");
        break;
    case AvatarState::UndoAsk:
        file = QStringLiteral("undo.jpg");
        mood = QStringLiteral("什么？你要悔棋？！");
        break;
    case AvatarState::Listening:
        file = QStringLiteral("music.jpg");
        mood = QStringLiteral("♪ 沉浸音乐中…");
        break;
    case AvatarState::Lose:
        file = QStringLiteral("lose.jpg");
        mood = QStringLiteral("呜呜…我输了");
        break;
    case AvatarState::Win:
        file = QStringLiteral("win.jpg");
        mood = QStringLiteral("你真是菜坤");
        break;
    }

    if (m_avatarMovie->state() == QMovie::Running)
        m_avatarMovie->stop();
    m_avatarImg->setPixmap(QPixmap(QStringLiteral(":/avatar/") + file)
        .scaled(m_avatarImg->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_avatarMood->setText(mood);
}

// 玩家吃掉 AI 棋子：先愤怒脸 0.8 秒，再做 gif 动作 1.3 秒，最后回到思考/待机
void MainWindow::playCaptureReaction()
{
    m_avatarTimer->stop();
    setAvatar(AvatarState::Angry);
    m_avatarTimer->start(800);
}

void MainWindow::onAvatarTimer()
{
    if (m_avatarState == AvatarState::Angry && m_avatarMovie->isValid()) {
        setAvatar(AvatarState::Action);
        m_avatarTimer->start(1300);   // gif 实际时长 1.3 秒，正好播一遍
        return;
    }
    // 动作播完（或没有 gif）：终局形象由胜负流程负责，这里不动
    if (m_gameOver)
        return;
    setAvatar(m_aiThinking ? AvatarState::Thinking : AvatarState::Idle);
}

void MainWindow::selectPiece(int row, int col)
{
    m_selRow = row;
    m_selCol = col;
    m_sndSelect->play();
    // 玩家选中棋子：对面切换成观察脸（思考/反应序列/终局时不打扰）
    if (!m_gameOver && !m_aiThinking && !m_avatarTimer->isActive())
        setAvatar(AvatarState::Selected);
    refreshHints();
}

void MainWindow::clearSelection()
{
    m_selRow = -1;
    m_selCol = -1;
    m_hints.clear();
}

// 取玩家方全部合法走法，筛出当前选中棋子可走的那些
void MainWindow::refreshHints()
{
    m_hints.clear();
    if (m_selRow < 0)
        return;
    const std::vector<Move> all = generateAllMoves(coreBoard, m_playerRed);
    for (const Move &m : all) {
        if (m.x1 == m_selRow && m.y1 == m_selCol)
            m_hints.push_back(m);
    }
}

// 在 coreBoard 上撤销一步（棋子归位、被吃子放回）
void MainWindow::revertStep(const Step &s)
{
    coreBoard[s.move.x1][s.move.y1] = s.piece;
    coreBoard[s.move.x2][s.move.y2] = s.captured;
}

void MainWindow::refreshButtons()
{
    m_undoBtn->setEnabled(!m_history.empty() && !m_aiThinking);
    m_restartBtn->setEnabled(!m_aiThinking);
    m_levelCombo->setEnabled(!m_aiThinking);
    m_sideCombo->setEnabled(!m_aiThinking);
    m_resignBtn->setEnabled(!m_gameOver && !m_aiThinking);
}

// 刷新双方被将军状态：状态栏文字 + 呼吸动画开关
void MainWindow::updateCheckState()
{
    m_redInCheck = isInCheck(coreBoard, true);
    m_blackInCheck = isInCheck(coreBoard, false);

    if (m_redInCheck || m_blackInCheck) {
        if (!m_animTimer->isActive())
            m_animTimer->start();
    } else {
        m_animTimer->stop();
    }

    // 将军音效：我方被将军放我方音，对面被将军放对面音；终局将死不放（放胜负音效）
    const bool playerInCheck = m_playerRed ? m_redInCheck : m_blackInCheck;
    const bool aiInCheck = m_playerRed ? m_blackInCheck : m_redInCheck;
    const bool wasPlayerInCheck = m_playerRed ? m_wasRedInCheck : m_wasBlackInCheck;
    const bool wasAiInCheck = m_playerRed ? m_wasBlackInCheck : m_wasRedInCheck;
    if (playerInCheck && !wasPlayerInCheck && !m_gameOver)
        m_sndCheckRed->play();
    if (aiInCheck && !wasAiInCheck && !m_gameOver)
        m_sndCheck->play();
    m_wasRedInCheck = m_redInCheck;
    m_wasBlackInCheck = m_blackInCheck;

    if (m_gameOver) {
        // 状态栏由胜负弹窗流程另行设置，这里不动
    } else if (m_aiThinking) {
        const QString side = m_playerRed ? QStringLiteral("黑方") : QStringLiteral("红方");
        statusBar()->showMessage(side + QStringLiteral("思考中…"));
    } else if (m_redInCheck) {
        if (m_playerRed)
            statusBar()->showMessage(QStringLiteral("⚠ 将军！你必须应将"));
        else
            statusBar()->showMessage(QStringLiteral("将军！红方已被将军"));
    } else if (m_blackInCheck) {
        if (m_playerRed)
            statusBar()->showMessage(QStringLiteral("红方走棋 —— 黑方已被将军！"));
        else
            statusBar()->showMessage(QStringLiteral("⚠ 将军！你必须应将"));
    } else {
        if (m_playerRed)
            statusBar()->showMessage(QStringLiteral("红方走棋（你执红先行）"));
        else
            statusBar()->showMessage(QStringLiteral("黑方走棋（你执黑先行）"));
    }
}

void MainWindow::restartGame()
{
    if (m_aiThinking)
        return;

    initBoard();
    resetGameHash();
    resetKillerAndHistory();
    m_history.clear();
    clearSelection();
    m_hasLast = false;
    m_gameOver = false;
    m_aiThinking = false;
    m_animPhase = 0.0;
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    m_avatarTimer->stop();
    setAvatar(AvatarState::Idle);
    updateCheckState();
    refreshButtons();
    m_view->update();
}

void MainWindow::undoMove()
{
    if (m_history.empty() || m_aiThinking)
        return;

    // 对面抱头震惊，同时弹确认框
    m_avatarTimer->stop();
    setAvatar(AvatarState::UndoAsk);
    const auto ret = QMessageBox::question(this,
        QStringLiteral("悔棋"),
        QStringLiteral("你是否要悔棋？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        setAvatar(m_selRow >= 0 ? AvatarState::Selected : AvatarState::Idle);
        return;
    }

    // 末尾若是 AI 的应手，先撤它；再撤玩家那步（一次悔棋回两手）
    if (m_history.back().byRed == !m_playerRed) {
        revertStep(m_history.back());
        m_history.pop_back();
    }
    if (!m_history.empty() && m_history.back().byRed == m_playerRed) {
        revertStep(m_history.back());
        m_history.pop_back();
    }

    m_gameOver = false;
    clearSelection();
    m_avatarTimer->stop();
    setAvatar(AvatarState::Idle);
    if (m_history.empty()) {
        m_hasLast = false;
        resetGameHash();
        resetKillerAndHistory();
    } else {
        m_last = m_history.back().move;
        m_hasLast = true;
    }
    updateCheckState();
    refreshButtons();
    m_view->update();
}

// 投降：直接判负，对面大笑（咧嘴笑 + 视频笑声）
void MainWindow::resignGame()
{
    if (m_gameOver || m_aiThinking)
        return;

    m_gameOver = true;
    clearSelection();
    m_animTimer->stop();
    m_avatarTimer->stop();
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    const QString winner = m_playerRed ? QStringLiteral("黑方") : QStringLiteral("红方");
    statusBar()->showMessage(QStringLiteral("你投降了 —— %1胜利！").arg(winner));
    setAvatar(AvatarState::Win);
    m_sndLose->play();
    refreshButtons();
    m_view->update();
}

// 逻辑坐标 -> 显示坐标（玩家执黑时棋盘整体旋转 180°）
void MainWindow::displayPt(int row, int col, int &dr, int &dc) const
{
    if (m_playerRed) { dr = row; dc = col; }
    else             { dr = kRows - 1 - row; dc = kCols - 1 - col; }
}

// 显示坐标 -> 逻辑坐标（旋转 180° 的逆变换就是它自己）
void MainWindow::logicalPt(int row, int col, int &lr, int &lc) const
{
    displayPt(row, col, lr, lc);
}

// 把用户选择的文件复制到 exe 旁 assets/ 目录（防止源文件被移动/删除后失效）
QString MainWindow::copyToAssets(const QString &src, const QString &tag)
{
    QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/assets"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return QString();
    const QString dst = dir.filePath(tag + QStringLiteral(".") + QFileInfo(src).suffix());
    if (QFileInfo::exists(dst))
        QFile::remove(dst);
    return QFile::copy(src, dst) ? dst : QString();
}

// 启动时恢复上次用过的自定义背景/音乐
void MainWindow::loadCustomAssets()
{
    QSettings st(QStringLiteral("candyboard"), QStringLiteral("gui"));
    m_customBgPath = st.value(QStringLiteral("customBg")).toString();
    m_customMusicPath = st.value(QStringLiteral("customMusic")).toString();
    if (!m_customBgPath.isEmpty() && QFileInfo::exists(m_customBgPath)) {
        m_bgCombo->setItemText(3, QStringLiteral("自定义背景"));
    } else {
        m_customBgPath.clear();
    }
    if (!m_customMusicPath.isEmpty() && QFileInfo::exists(m_customMusicPath)) {
        m_musicCombo->setItemText(4, QStringLiteral("自定义音乐"));
    } else {
        m_customMusicPath.clear();
    }
}

// 按 m_bgIndex 加载背景图到缓存（paint 时只做 drawPixmap，不再重复解码）
void MainWindow::loadBgPixmap()
{
    if (m_bgIndex == 0) {
        m_bgPixmap = QPixmap();   // 木质用渐变画，不需要图片
    } else if (m_bgIndex == 1) {
        m_bgPixmap = QPixmap(QStringLiteral(":/bg/blue.jpg"));
    } else if (m_bgIndex == 2) {
        m_bgPixmap = QPixmap(QStringLiteral(":/bg/dragon.jpg"));
    } else {
        m_bgPixmap = QPixmap(m_customBgPath);
    }
}

// 切换棋盘背景；最后一项“自定义…”使用/上传用户自己的图
void MainWindow::onBgChanged(int index)
{
    if (index == 3) {
        // 已有保存的自定义背景：询问直接用 / 重新上传 / 删除
        if (!m_customBgPath.isEmpty()) {
            QMessageBox box(this);
            box.setWindowTitle(QStringLiteral("自定义背景"));
            box.setText(QStringLiteral("已有保存的自定义背景。"));
            auto *useBtn = box.addButton(QStringLiteral("直接使用"), QMessageBox::AcceptRole);
            auto *repickBtn = box.addButton(QStringLiteral("重新选择"), QMessageBox::ActionRole);
            auto *delBtn = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            QAbstractButton *clicked = box.clickedButton();
            if (clicked == useBtn) {
                m_bgIndex = 3;
                loadBgPixmap();
                m_view->update();
                return;
            }
            if (clicked == delBtn) {
                QFile::remove(m_customBgPath);   // 删除 assets/ 里的文件
                m_customBgPath.clear();
                QSettings(QStringLiteral("candyboard"), QStringLiteral("gui"))
                    .remove(QStringLiteral("customBg"));
                m_bgCombo->setItemText(3, QStringLiteral("自定义…"));
                m_bgCombo->blockSignals(true);
                m_bgCombo->setCurrentIndex(0);
                m_bgCombo->blockSignals(false);
                m_bgIndex = 0;
                loadBgPixmap();
                m_view->update();
                return;
            }
            if (clicked != repickBtn) {   // Cancel：退回原背景
                m_bgCombo->blockSignals(true);
                m_bgCombo->setCurrentIndex(m_bgIndex);
                m_bgCombo->blockSignals(false);
                return;
            }
            // repickBtn：落入下面的文件选择框
        }
        const QString f = QFileDialog::getOpenFileName(this,
            QStringLiteral("选择背景图片"), QString(),
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (f.isEmpty()) {   // 用户取消：退回原来的背景
            m_bgCombo->blockSignals(true);
            m_bgCombo->setCurrentIndex(m_bgIndex);
            m_bgCombo->blockSignals(false);
            return;
        }
        const QString dst = copyToAssets(f, QStringLiteral("custom_bg"));
        if (dst.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("背景"), QStringLiteral("复制图片失败，未更改背景。"));
            m_bgCombo->blockSignals(true);
            m_bgCombo->setCurrentIndex(m_bgIndex);
            m_bgCombo->blockSignals(false);
            return;
        }
        m_customBgPath = dst;
        QSettings(QStringLiteral("candyboard"), QStringLiteral("gui"))
            .setValue(QStringLiteral("customBg"), dst);
        m_bgCombo->setItemText(3, QStringLiteral("自定义背景"));
    }
    m_bgIndex = index;
    loadBgPixmap();
    m_view->update();
}

// 切换背景音乐：无声 / 三首内置歌曲 / 自定义上传
// 开启时：形象锁定为听歌脸、所有音效静音；关闭时恢复正常
void MainWindow::onMusicChanged(int index)
{
    if (index == 4) {
        // 已有保存的自定义音乐：询问直接用 / 重新上传 / 删除
        bool needPick = m_customMusicPath.isEmpty();
        if (!needPick) {
            QMessageBox box(this);
            box.setWindowTitle(QStringLiteral("自定义音乐"));
            box.setText(QStringLiteral("已有保存的自定义音乐。"));
            auto *useBtn = box.addButton(QStringLiteral("直接播放"), QMessageBox::AcceptRole);
            auto *repickBtn = box.addButton(QStringLiteral("重新选择"), QMessageBox::ActionRole);
            auto *delBtn = box.addButton(QStringLiteral("删除"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            QAbstractButton *clicked = box.clickedButton();
            if (clicked == delBtn) {
                m_musicPlayer->stop();          // 万一正在放这首，先停掉
                m_musicOn = false;
                setSfxMuted(false);
                setAvatar(AvatarState::Idle);
                QFile::remove(m_customMusicPath);
                m_customMusicPath.clear();
                QSettings(QStringLiteral("candyboard"), QStringLiteral("gui"))
                    .remove(QStringLiteral("customMusic"));
                m_musicCombo->setItemText(4, QStringLiteral("自定义…"));
                m_musicCombo->blockSignals(true);
                m_musicCombo->setCurrentIndex(0);
                m_musicCombo->blockSignals(false);
                m_musicSel = 0;
                return;
            }
            if (clicked == useBtn) {
                // 落入底部播放已有文件
            } else if (clicked == repickBtn) {
                needPick = true;
            } else {   // Cancel：退回上次确认的选择
                m_musicCombo->blockSignals(true);
                m_musicCombo->setCurrentIndex(m_musicSel);
                m_musicCombo->blockSignals(false);
                return;
            }
        }
        if (needPick) {
            const QString f = QFileDialog::getOpenFileName(this,
                QStringLiteral("选择音乐文件"), QString(),
                QStringLiteral("音频文件 (*.mp3 *.wav *.flac *.ogg *.m4a)"));
            if (f.isEmpty()) {   // 用户取消：退回上次确认的选择（保留旧文件）
                m_musicCombo->blockSignals(true);
                m_musicCombo->setCurrentIndex(m_musicSel);
                m_musicCombo->blockSignals(false);
                return;
            }
            const QString dst = copyToAssets(f, QStringLiteral("custom_music"));
            if (dst.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("音乐"), QStringLiteral("复制音频失败，未更改音乐。"));
                m_musicCombo->blockSignals(true);
                m_musicCombo->setCurrentIndex(m_musicSel);
                m_musicCombo->blockSignals(false);
                return;
            }
            m_customMusicPath = dst;
            QSettings(QStringLiteral("candyboard"), QStringLiteral("gui"))
                .setValue(QStringLiteral("customMusic"), dst);
        }
        m_musicCombo->setItemText(4, QStringLiteral("自定义音乐"));
    }

    if (index == 0) {
        m_musicPlayer->stop();
        m_musicOn = false;
        setSfxMuted(false);
        setAvatar(AvatarState::Idle);
        m_musicSel = 0;
        return;
    }
    QUrl src;
    if (index == 1)      src = QUrl(QStringLiteral("qrc:/music/song1.mp3"));
    else if (index == 2) src = QUrl(QStringLiteral("qrc:/music/song2.mp3"));
    else if (index == 3) src = QUrl(QStringLiteral("qrc:/music/song3.mp3"));
    else                 src = QUrl::fromLocalFile(m_customMusicPath);
    m_musicPlayer->setSource(src);
    m_musicPlayer->play();
    m_musicOn = true;
    m_musicSel = index;
    setSfxMuted(true);
    m_avatarTimer->stop();
    setAvatar(AvatarState::Listening);
}

// 一键静音/恢复全部音效（音乐开启时奶娃不听棋声）
void MainWindow::setSfxMuted(bool muted)
{
    QSoundEffect *all[] = {
        m_sndSelect, m_sndMove, m_sndCapture, m_sndCaptureRed,
        m_sndCheck, m_sndCheckRed, m_sndWin, m_sndLose
    };
    for (QSoundEffect *s : all) {
        if (s) s->setMuted(muted);
    }
}

// 切换先手/后手：重开一局，后手时 AI 执红先行
void MainWindow::onSideChanged(int index)
{
    if (m_aiThinking)
        return;
    const bool wantRed = (index == 0);
    if (wantRed == m_playerRed)
        return;
    m_playerRed = wantRed;
    restartGame();
    if (!m_playerRed)
        startAiThinking();   // 玩家后手：AI（红）先走
}

// 拷贝当前棋盘，在后台线程启动 AI 搜索（搜索只碰副本，coreBoard 不变）
void MainWindow::startAiThinking()
{
    m_aiThinking = true;
    refreshButtons();
    // 若被吃子的愤怒/动作序列还在播，让它播完再切思考脸
    if (!m_avatarTimer->isActive())
        setAvatar(AvatarState::Thinking);

    m_aiThread = new AiThread();
    std::memcpy(m_aiThread->m_board, coreBoard, sizeof(coreBoard));
    m_aiThread->m_timeMs = m_levelCombo->currentData().toInt();
    m_aiThread->m_redSide = !m_playerRed;   // AI 执玩家的对面
    // finished 从工作线程发出，队列到主线程执行；先 onAiFinished 读后再回收
    connect(m_aiThread, &QThread::finished, this, &MainWindow::onAiFinished);
    connect(m_aiThread, &QThread::finished, m_aiThread, &QObject::deleteLater);

    m_thinkClock->restart();
    m_thinkTimer->start();
    onAiTick();
    m_aiThread->start();
}

void MainWindow::onAiTick()
{
    const double sec = m_thinkClock->elapsed() / 1000.0;
    const QString side = m_playerRed ? QStringLiteral("黑方") : QStringLiteral("红方");
    statusBar()->showMessage(side + QStringLiteral("思考中… %1 秒").arg(sec, 0, 'f', 1));
}

// AI 搜索结束（主线程）：把结果落到真棋盘，再判红方死活
void MainWindow::onAiFinished()
{
    const Move ai = m_aiThread->m_result;   // 此时 run() 已返回，结果有效
    m_aiThread = nullptr;                   // 对象由 deleteLater 回收
    m_thinkTimer->stop();
    m_aiThinking = false;

    if (!(inBoard(ai.x1, ai.y1) && inBoard(ai.x2, ai.y2))) {
        // 兜底：理论上玩家落子后的 checkGameOver 已挡住无棋可走
        m_gameOver = true;
        statusBar()->showMessage(QStringLiteral("你赢了！（对面无棋可走）"));
        m_animTimer->stop();
        setAvatar(AvatarState::Lose);
        m_sndWin->play();
        m_view->update();
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("糖蛋赢了，敢不敢挑战更聪明的我？"));
        refreshButtons();
        return;
    }

    Step aiStep;
    aiStep.captured = coreBoard[ai.x2][ai.y2];
    aiStep.piece = coreBoard[ai.x1][ai.y1];
    executeMove(ai);
    aiStep.move = ai;
    aiStep.byRed = !m_playerRed;
    m_history.push_back(aiStep);
    m_last = ai;
    m_hasLast = true;
    // AI 落子：对面吃子用对面音效，普通走动用木质落子声
    (aiStep.captured != 0 ? m_sndCapture : m_sndMove)->play();

    // 先判终局再刷新将军状态，将死时只放落败音
    const int playerState = checkGameOver(coreBoard, m_playerRed);
    if (playerState != 0)
        m_gameOver = true;
    updateCheckState();
    m_view->update();

    if (playerState != 0) {
        const QString kill = QString::fromLocal8Bit(
            getKillTypeName(coreBoard, m_playerRed).c_str());
        const QString reason = (playerState == 1)
            ? QStringLiteral("你被将死")
            : QStringLiteral("你无子可动（困毙）");
        statusBar()->showMessage(QStringLiteral("对面胜利！") + reason);
        m_animTimer->stop();
        setAvatar(AvatarState::Win);
        m_sndLose->play();
        m_view->update();
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("对面胜利！\n%1\n杀法：%2").arg(reason, kill));
    } else {
        setAvatar(AvatarState::Idle);
    }
    refreshButtons();
}

void MainWindow::handleBoardPress(const QPoint &pos, const QSize &viewSize,
                                  bool rightButton)
{
    if (m_aiThinking)
        return;

    const Geo g = makeGeo(viewSize);
    int row = -1, col = -1;

    if (rightButton) {
        clearSelection();
        // 右键取消选中：形象回到平时状态
        if (!m_gameOver && !m_aiThinking && !m_avatarTimer->isActive())
            setAvatar(AvatarState::Idle);
        m_view->update();
        return;
    }
    if (m_gameOver || !hitCross(g, pos, row, col))
        return;

    // 显示坐标 -> 逻辑坐标（执黑时棋盘旋转 180°）
    logicalPt(row, col, row, col);
    const int piece = coreBoard[row][col];
    // “我方棋子”判定：执子方颜色随选择变化
    const auto isMine = [this](int p) {
        return p != 0 && (isRed(p) == m_playerRed);
    };

    if (m_selRow < 0) {
        // 还没选中：只能选自己的子
        if (isMine(piece))
            selectPiece(row, col);
    } else {
        if (isMine(piece)) {
            // 点到自己另一个棋子：换选
            selectPiece(row, col);
        } else {
            // 先记下目标点上的子（movePiece 成功后该位置会被覆盖）
            const int captured = coreBoard[row][col];

            // 尝试走子（movePiece 内部校验走法、将帅对脸、送将）
            if (!movePiece(m_selRow, m_selCol, row, col)) {
                m_view->update();
                return;
            }

            // ---- 玩家落子成立，记入历史 ----
            Step playerStep;
            playerStep.move = Move(m_selRow, m_selCol, row, col);
            playerStep.piece = coreBoard[row][col];
            playerStep.captured = captured;
            playerStep.byRed = m_playerRed;
            m_history.push_back(playerStep);
            m_last = playerStep.move;
            m_hasLast = true;
            // 落子音：我方吃子用我方音效，普通走动用木质落子声
            (captured != 0 ? m_sndCaptureRed : m_sndMove)->play();
            // 吃掉 AI 棋子：对面形象先做愤怒脸 -> gif 动作（与吃子音同步）
            if (captured != 0)
                playCaptureReaction();
            clearSelection();
            refreshButtons();

            // ---- 玩家走完，轮到 AI：先判终局（将死则只放胜利音，不再响将军警报）----
            const bool aiRed = !m_playerRed;
            const int aiState = checkGameOver(coreBoard, aiRed);
            if (aiState != 0)
                m_gameOver = true;
            updateCheckState();
            m_view->update();

            if (aiState != 0) {
                const QString kill = QString::fromLocal8Bit(
                    getKillTypeName(coreBoard, aiRed).c_str());
                const QString reason = (aiState == 1)
                    ? QStringLiteral("对面被将死")
                    : QStringLiteral("对面无子可动（困毙）");
                statusBar()->showMessage(QStringLiteral("你赢了！") + reason);
                m_animTimer->stop();
                m_avatarTimer->stop();
                setAvatar(AvatarState::Lose);
                m_sndWin->play();
                m_view->update();
                QMessageBox::information(this,
                    QStringLiteral("对局结束"),
                    QStringLiteral("🎉 你赢了！\n%1\n杀法：%2").arg(reason, kill));
                refreshButtons();
                return;
            }

            // ---- AI 后台应招（按难度 10/15/20 秒）----
            // 玩家将军且未吃子（吃子已播愤怒序列）：对面点赞 0.9 秒再思考
            const bool aiInCheck = m_playerRed ? m_blackInCheck : m_redInCheck;
            if (captured == 0 && aiInCheck) {
                m_avatarTimer->stop();
                setAvatar(AvatarState::GoodMove);
                m_avatarTimer->start(900);
            }
            startAiThinking();
        }
    }
    m_view->update();
}

// ============================================================================
// 绘制
// ============================================================================
void MainWindow::renderBoard(QPainter &p, const QSize &viewSize)
{
    const Geo g = makeGeo(viewSize);

    // ---- 1. 背景：经典木质 / 蓝发少女 / 魔龙降临 / 自定义（图片已缓存，paint 零解码）----
    if (m_bgPixmap.isNull()) {
        QLinearGradient bg(0, 0, 0, viewSize.height());
        bg.setColorAt(0.0, QColor(247, 230, 194));
        bg.setColorAt(1.0, QColor(233, 198, 142));
        p.fillRect(QRectF(QPointF(0, 0), viewSize), bg);
    } else {
        p.drawPixmap(QRectF(QPointF(0, 0), viewSize), m_bgPixmap, m_bgPixmap.rect());
    }

    // ---- 2. 棋盘外框（双线）----
    const qreal frame = 12.0;
    const QRectF outer(crossPt(g, 0, 0) - QPointF(frame, frame),
                       crossPt(g, kRows - 1, kCols - 1) + QPointF(frame, frame));
    p.setPen(QPen(kLineColor, 3.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(outer);
    p.setPen(QPen(kLineColor, 1.0));
    p.drawRect(outer.adjusted(5, 5, -5, -5));

    // ---- 3. 网格线 ----
    p.setPen(QPen(kLineColor, 1.6));
    // 10 条横线
    for (int r = 0; r < kRows; ++r)
        p.drawLine(crossPt(g, r, 0), crossPt(g, r, kCols - 1));
    // 竖线：最左/最右贯通；中间 7 条在楚河汉界处断开
    for (int c = 0; c < kCols; ++c) {
        if (c == 0 || c == kCols - 1) {
            p.drawLine(crossPt(g, 0, c), crossPt(g, kRows - 1, c));
        } else {
            p.drawLine(crossPt(g, 0, c), crossPt(g, 4, c));
            p.drawLine(crossPt(g, 5, c), crossPt(g, kRows - 1, c));
        }
    }

    // ---- 4. 上下九宫斜线 ----
    p.setPen(QPen(kLineColor, 1.3));
    p.drawLine(crossPt(g, 0, 3), crossPt(g, 2, 5));
    p.drawLine(crossPt(g, 0, 5), crossPt(g, 2, 3));
    p.drawLine(crossPt(g, 7, 3), crossPt(g, 9, 5));
    p.drawLine(crossPt(g, 7, 5), crossPt(g, 9, 3));

    // ---- 5. 楚河 / 汉界 ----
    QFont riverFont(QStringLiteral("KaiTi"));
    riverFont.setStyleHint(QFont::Serif);
    riverFont.setBold(true);
    riverFont.setPixelSize(int(g.cell * 0.46));
    p.setFont(riverFont);
    p.setPen(kRiverColor);
    const qreal riverY = g.oy + 4.5 * g.cell;
    const QRectF riverLeft(g.ox, riverY - g.cell * 0.45,
                           4.0 * g.cell, g.cell * 0.9);
    const QRectF riverRight(g.ox + 4.0 * g.cell, riverY - g.cell * 0.45,
                            4.0 * g.cell, g.cell * 0.9);
    p.drawText(riverLeft, Qt::AlignCenter, QStringLiteral("楚　河"));
    p.drawText(riverRight, Qt::AlignCenter, QStringLiteral("漢　界"));

    // ---- 6. 炮位、兵位小十字标记 ----
    p.setPen(QPen(kLineColor, 1.4));
    const int ticks[][2] = {
        {2, 1}, {2, 7}, {7, 1}, {7, 7},                 // 炮
        {3, 0}, {3, 2}, {3, 4}, {3, 6}, {3, 8},         // 黑卒
        {6, 0}, {6, 2}, {6, 4}, {6, 6}, {6, 8}          // 红兵
    };
    for (const auto &t : ticks)
        drawCornerTicks(p, g, t[0], t[1]);

    // ---- 7. 上一步起止位置高亮（画在棋子下面）----
    // dp：逻辑坐标 -> 显示坐标（执黑时旋转 180°）
    auto dp = [this, &g](int row, int col) {
        int dr, dc;
        displayPt(row, col, dr, dc);
        return crossPt(g, dr, dc);
    };
    if (m_hasLast) {
        p.setPen(Qt::NoPen);
        p.setBrush(kLastColor);
        const qreal hl = g.cell * 0.46;
        p.drawEllipse(dp(m_last.x1, m_last.y1), hl, hl);
        p.drawEllipse(dp(m_last.x2, m_last.y2), hl, hl);
    }

    // ---- 8. 棋子 ----
    const qreal radius = g.cell * 0.43;
    QFont pieceFont(QStringLiteral("KaiTi"));
    pieceFont.setStyleHint(QFont::Serif);
    pieceFont.setBold(true);
    pieceFont.setPixelSize(int(g.cell * 0.52));

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const int code = coreBoard[r][c];
            if (code == 0)
                continue;

            const QPointF center = dp(r, c);

            // 阴影
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 45));
            p.drawEllipse(center + QPointF(1.5, 2.5), radius, radius);

            // 棋身：象牙色径向渐变
            QRadialGradient body(center - QPointF(radius * 0.35, radius * 0.35),
                                 radius * 1.25);
            body.setColorAt(0.0, QColor(255, 251, 238));
            body.setColorAt(0.7, QColor(247, 228, 190));
            body.setColorAt(1.0, QColor(226, 192, 138));
            p.setBrush(body);
            p.setPen(QPen(kLineColor, 2.2));
            p.drawEllipse(center, radius, radius);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(140, 96, 48), 1.0));
            p.drawEllipse(center, radius * 0.86, radius * 0.86);

            // 棋子字
            p.setFont(pieceFont);
            p.setPen(isRed(code) ? kRedText : kBlackText);
            const QRectF tb(center.x() - radius, center.y() - radius,
                            2 * radius, 2 * radius);
            p.drawText(tb, Qt::AlignCenter, pieceText(code));
        }
    }

    // ---- 9. 选中金圈 ----
    if (m_selRow >= 0) {
        const QPointF center = dp(m_selRow, m_selCol);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kSelectColor, 3.0));
        p.drawEllipse(center, radius + 2.5, radius + 2.5);
    }

    // ---- 10. 合法落点提示（空点画绿点，有敌子画红圈）----
    for (const Move &m : m_hints) {
        const QPointF center = dp(m.x2, m.y2);
        if (coreBoard[m.x2][m.y2] == 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(kHintColor);
            const qreal dot = g.cell * 0.13;
            p.drawEllipse(center, dot, dot);
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kCaptureColor, 3.0));
            p.drawEllipse(center, radius + 2.5, radius + 2.5);
        }
    }

    // ---- 11. 被将军的将帅：红色呼吸圈 ----
    if (m_redInCheck || m_blackInCheck) {
        int kr = -1, kc = -1;
        const bool found = m_redInCheck ? findPiece('K', kr, kc)
                                        : findPiece('k', kr, kc);
        if (found) {
            const qreal pulse = 0.5 + 0.5 * qSin(m_animPhase);   // 0..1
            QColor warn = kCheckColor;
            warn.setAlpha(int(110 + 120 * pulse));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(warn, 3.0 + 2.0 * pulse));
            p.drawEllipse(dp(kr, kc),
                          radius + 3.5 + 2.0 * pulse,
                          radius + 3.5 + 2.0 * pulse);
        }
    }
}
