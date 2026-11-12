#include "MainWindow.h"

#include "NetworkManager.h"
#include "StickerPicker.h"
#include "StartScreen.h"
#include "ChatDialog.h"
#include "ServerConfig.h"
#include "Achievements.h"

#include <QApplication>
#include <QAudioOutput>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDockWidget>
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
#include <QLineEdit>
#include <QLineF>
#include <QLinearGradient>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMovie>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QRectF>
#include <QRegularExpression>
#include <QSettings>
#include <QShowEvent>
#include <QSize>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSoundEffect>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <cstring>

#include "board.h"

// ============================================================================
// 奶龙登场动画：模态对话框，QMovie 播跳舞 gif + QSoundEffect 播配套音乐，
// 播完或点跳过即关闭。不依赖系统 Media Foundation（部分机器 MF 播放 mp4
// 会立刻报 EndOfMedia），gif/wav 全部直接走 qrc
// ============================================================================
namespace {

class DragonVideoDialog : public QDialog
{
public:
    explicit DragonVideoDialog(QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("奶龙登场！"));
        setModal(true);
        resize(520, 580);

        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 8);

        m_scene = new QLabel(this);
        m_scene->setAlignment(Qt::AlignCenter);
        m_scene->setFixedSize(480, 480);   // 与 gif 同尺寸，画面正好铺满、四周不露底色
        lay->addWidget(m_scene, 0, Qt::AlignHCenter);

        auto *row = new QHBoxLayout();
        row->setContentsMargins(10, 0, 10, 0);
        auto *tip = new QLabel(QStringLiteral("奶龙登场中…"), this);
        auto *skip = new QPushButton(QStringLiteral("跳过 >>"), this);
        row->addWidget(tip);
        row->addStretch();
        row->addWidget(skip);
        lay->addLayout(row);

        m_movie = new QMovie(QStringLiteral(":/videos/dragon_intro.gif"), QByteArray(), this);
        m_scene->setMovie(m_movie);

        m_music = new QSoundEffect(this);
        m_music->setSource(QUrl(QStringLiteral("qrc:/videos/dragon_intro.wav")));
        m_music->setVolume(0.9f);

        connect(skip, &QPushButton::clicked, this, [this]() { accept(); });

        // gif 最后一帧稍停即关闭（gif 约 5 秒，与 wav 等长）
        connect(m_movie, &QMovie::frameChanged, this, [this](int frameNo) {
            const int total = m_movie->frameCount();
            if (!m_finishing && total > 0 && frameNo >= total - 1) {
                m_finishing = true;
                QTimer::singleShot(200, this, &QDialog::accept);
            }
        });
        // 兜底：素材异常时最多 9 秒自动关
        QTimer::singleShot(9000, this, [this]() { accept(); });
    }

    void showEvent(QShowEvent *ev) override
    {
        QDialog::showEvent(ev);
        if (!m_started) {
            m_started = true;
            m_movie->start();
            m_music->play();
        }
    }

    ~DragonVideoDialog() override
    {
        m_music->stop();
        m_movie->stop();
    }

private:
    QLabel *m_scene;
    QMovie *m_movie;
    QSoundEffect *m_music;
    bool m_started = false;
    bool m_finishing = false;
};

} // namespace

// ============================================================================
// 棋盘几何：所有渲染和“像素 <-> 棋盘坐标”换算的唯一数据源
// 棋盘 9 列 10 行交叉点；棋子摆在交叉点上。
// ============================================================================
namespace {

constexpr int kRows = 10;
constexpr int kCols = 9;
#ifdef Q_OS_ANDROID
constexpr qreal kMargin = 14.0;   // 手机屏幕窄，棋盘外沿留白收小
#else
constexpr qreal kMargin = 30.0;   // 棋盘外沿留白（炮位/兵位小十字需要空间）
#endif

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
#ifdef Q_OS_ANDROID
    setMinimumSize(0, 0);   // 手机上让棋盘自由填满剩余空间
#else
    setMinimumSize(480, 540);
#endif
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
MainWindow::MainWindow(GameMode mode, bool isHost, NetworkManager *net,
                       bool playerRed, bool isSpectator, int endgameIdx,
                       int levelIdx, const PlayerCardInfo &self,
                       const PlayerCardInfo &opponent, QWidget *parent)
    : QMainWindow(parent),
      m_selfInfo(self), m_oppInfo(opponent),
      m_playerRed(playerRed), m_isSpectator(isSpectator),
      m_endgameIdx(endgameIdx), m_mode(mode), m_isHost(isHost), m_net(net)
{
    ++s_openGameWindows;   // 对局/观战窗打开：期间收到的私信只入缓存不弹窗
    m_openCounted = true;
    // 进对局前可能正开着私信聊天窗：顺手隐藏（不关闭，聊天记录保留），避免挡棋盘
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (auto *dlg = qobject_cast<ChatDialog*>(w)) {
            if (dlg->isVisible())
                dlg->hide();
        }
    }
    // 联机模式：playerRed 由调用方传入（随机红黑方或互联网匹配结果）
    if (m_mode == GameMode::Online) {
        m_myTurn = m_playerRed;   // 红方先走
    }

    setWindowTitle(m_mode == GameMode::Online
                       ? QStringLiteral("中国象棋 - 联机对战")
                       : m_endgameIdx >= 0
                         ? QStringLiteral("中国象棋 - 残局挑战")
                         : QStringLiteral("中国象棋 - candyboard"));

    // 顶部按钮行 + 棋盘
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
#ifdef Q_OS_ANDROID
    root->setContentsMargins(4, 4, 4, 0);
    root->setSpacing(2);
#else
    root->setContentsMargins(8, 8, 8, 0);
    root->setSpacing(6);
#endif

    auto *btnRow = new QHBoxLayout();
    // 返回主菜单：所有模式通用（观战也有），关窗即回主界面
    m_backBtn = new QPushButton(QStringLiteral("返回主菜单"), central);
    btnRow->addWidget(m_backBtn);
    btnRow->addSpacing(8);
    connect(m_backBtn, &QPushButton::clicked, this, &MainWindow::goBackToMenu);
    m_restartBtn = new QPushButton(QStringLiteral("重新开局"), central);
    m_undoBtn = new QPushButton(QStringLiteral("悔棋"), central);
    m_resignBtn = new QPushButton(QStringLiteral("投降"), central);
    // 观战者：隐藏所有控制按钮
    if (m_isSpectator) {
        m_restartBtn->setVisible(false);
        m_undoBtn->setVisible(false);
        m_resignBtn->setVisible(false);
    }
    btnRow->addWidget(m_restartBtn);
    btnRow->addWidget(m_undoBtn);
    btnRow->addWidget(m_resignBtn);
    // 糖果指导：消耗 1 颗糖果让 AI 替我走一步（单机/局域网房客/互联网可用）
    if (candyAllowed()) {
        m_candyBtn = new QPushButton(central);
        btnRow->addWidget(m_candyBtn);
        connect(m_candyBtn, &QPushButton::clicked, this, &MainWindow::useCandy);
    }
    refreshCandyButton();
    // 联机房主专属：召唤奶龙给走子建议（不替你落子）
    if (m_mode == GameMode::Online && m_isHost && !m_isSpectator) {
        m_summonBtn = new QPushButton(QStringLiteral("🐲 奶娃助阵"), central);
        btnRow->addWidget(m_summonBtn);
        // 挂机按钮：奶龙登场后才出现
        m_afkBtn = new QPushButton(QStringLiteral("🥱 奶娃挂机"), central);
        m_afkBtn->setVisible(false);
        btnRow->addWidget(m_afkBtn);
    }
    btnRow->addSpacing(16);
#ifdef Q_OS_ANDROID
    btnRow->addWidget(new QLabel(QStringLiteral("难度："), central));
#else
    btnRow->addWidget(new QLabel(QStringLiteral("AI 难度："), central));
#endif
    m_levelCombo = new QComboBox(central);   // 必须先 new 出下拉框，才能往里 addItem
    m_levelCombo->addItem(QStringLiteral("笨糖果（10 秒）"), 10000);
    m_levelCombo->addItem(QStringLiteral("一般糖果（15 秒）"), 15000);
    m_levelCombo->addItem(QStringLiteral("聪明糖果（20 秒）"), 20000);
    m_levelCombo->setCurrentIndex(0);   // 默认"笨糖果"
    if (m_isSpectator)
        m_levelCombo->setVisible(false);
    btnRow->addWidget(m_levelCombo);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 第二行：背景 / 音乐 / 音量 / 先后手
    // 手机竖屏宽度不够，Android 上把“音量/执子”拆到第三行
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
#ifdef Q_OS_ANDROID
    // 收起时按较短宽度显示，点开下拉仍能看到完整曲名
    m_bgCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_musicCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_bgCombo->setMinimumContentsLength(4);
    m_musicCombo->setMinimumContentsLength(8);
    optRow->addStretch();
    root->addLayout(optRow);

    auto *optRow2 = new QHBoxLayout();
#else
    optRow->addSpacing(12);
#endif
    auto *volLabel = new QLabel(QStringLiteral("音量："), central);
    m_volSlider = new QSlider(Qt::Horizontal, central);
    m_volSlider->setRange(0, 100);
    m_volSlider->setValue(50);
    m_volSlider->setFixedWidth(110);
    auto *sideLabel = new QLabel(QStringLiteral("执子："), central);
    m_sideCombo = new QComboBox(central);
    m_sideCombo->addItem(QStringLiteral("先手（执红）"));
    m_sideCombo->addItem(QStringLiteral("后手（执黑）"));
    if (m_isSpectator) {
        sideLabel->setVisible(false);
        m_sideCombo->setVisible(false);
    }
#ifdef Q_OS_ANDROID
    optRow2->addWidget(volLabel);
    optRow2->addWidget(m_volSlider);
    optRow2->addSpacing(12);
    optRow2->addWidget(sideLabel);
    optRow2->addWidget(m_sideCombo);
    optRow2->addStretch();
    root->addLayout(optRow2);
#else
    optRow->addWidget(volLabel);
    optRow->addWidget(m_volSlider);
    optRow->addSpacing(12);
    optRow->addWidget(sideLabel);
    optRow->addWidget(m_sideCombo);
    optRow->addStretch();
    root->addLayout(optRow);
#endif

    // 背景音乐播放器：QMediaPlayer/QAudioOutput 构造会同步初始化 Media
    // Foundation/WASAPI，耗时数秒——不在构造或窗口显示后创建（会卡点击、
    // 也会把事件循环堵到窗口假死），而是首次真正播放音乐时才懒加载
    // （ensureMusicPlayer 幂等；onMusicChanged / summonDragon 入口已兜底）

    connect(m_bgCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onBgChanged);
    connect(m_musicCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onMusicChanged);
    connect(m_sideCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSideChanged);
    // 音量滑条 0-100 -> 0.0-1.0
    connect(m_volSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_audioOut)
            m_audioOut->setVolume(v / 100.0f);
    });

    m_view = new BoardView(this, central);

    // 右侧：虚拟形象面板（单机=AI 对面；联机=奶娃帮手）
    initAvatar();
    auto *contentRow = new QHBoxLayout();
    contentRow->setSpacing(8);

    // 棋盘列：上方黑方名片 + 棋盘 + 下方红方名片（按执子方，不随本机视角换位）
    auto *boardCol = new QVBoxLayout();
#ifdef Q_OS_ANDROID
    boardCol->setSpacing(2);
#else
    boardCol->setSpacing(4);
#endif
    buildPlayerCards(central, boardCol);
    boardCol->insertWidget(1, m_view, 1);
    contentRow->addLayout(boardCol, 1);

    m_avatarPanel = new QFrame(central);
    m_avatarPanel->setObjectName(QStringLiteral("avatarFrame"));
    m_avatarPanel->setStyleSheet(QStringLiteral(
        "QFrame#avatarFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0.4, y2:1,"
        "    stop:0 #fffdf4, stop:0.55 #ffe9b8, stop:1 #e7c07f);"
        "  border: 2px solid #8a5a2a; border-radius: 14px;"
        "}"));
    // 投影：让卡片浮起来，营造伪3D感
    auto *shadow = new QGraphicsDropShadowEffect(m_avatarPanel);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(60, 36, 10, 110));
    m_avatarPanel->setGraphicsEffect(shadow);

    auto *nameLabel = new QLabel(
        m_mode == GameMode::Online ? QStringLiteral("奶娃帮手") : QStringLiteral("奶娃"),
        m_avatarPanel);
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
#ifdef Q_OS_ANDROID
    // 手机：形象面板压缩为顶部横向小条（图 48x48 + 名字/心情），把宽度让给棋盘
    m_avatarImg->setFixedSize(48, 48);
    m_avatarMovie->setScaledSize(QSize(48, 48));
    auto *avatarBar = new QHBoxLayout(m_avatarPanel);
    avatarBar->setContentsMargins(8, 2, 8, 2);
    avatarBar->setSpacing(8);
    auto *avatarText = new QVBoxLayout();
    avatarText->setSpacing(0);
    avatarText->addWidget(nameLabel);
    avatarText->addWidget(m_avatarMood);
    avatarBar->addWidget(m_avatarImg, 0, Qt::AlignVCenter);
    avatarBar->addLayout(avatarText, 1);
    m_avatarMood->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_avatarPanel->setFixedHeight(56);
    root->addWidget(m_avatarPanel);
#else
    auto *avatarCol = new QVBoxLayout(m_avatarPanel);
    avatarCol->setContentsMargins(12, 12, 12, 12);
    avatarCol->setSpacing(6);
    avatarCol->addWidget(nameLabel);
    avatarCol->addWidget(m_avatarImg, 0, Qt::AlignCenter);
    avatarCol->addWidget(m_avatarMood);
    avatarCol->addStretch();
    m_avatarPanel->setFixedWidth(200);
    contentRow->addWidget(m_avatarPanel);
#endif

    // 联机：奶娃形象默认隐藏，房主点"奶娃助阵"召唤后才出现（对面同步出现帮手条）
    if (m_mode == GameMode::Online)
        m_avatarPanel->setVisible(false);

    // 联机加入者：不显示右侧形象卡片，改为在棋盘上方（本方执黑时显示旋转
    // 180°，房主/红方阵地在屏幕上部）放一条“奶娃帮手”横条；房主召唤奶娃
    // 后出现，思考/打人等状态由房主端实时同步
    if (m_mode == GameMode::Online && !m_isHost) {
        m_avatarPanel->setVisible(false);
        m_peerBar = new QFrame(central);
        m_peerBar->setObjectName(QStringLiteral("peerBar"));
        m_peerBar->setStyleSheet(QStringLiteral(
            "QFrame#peerBar {"
            "  background: qlineargradient(x1:0, y1:0, x2:0.4, y2:1,"
            "    stop:0 #fffdf4, stop:0.55 #ffe9b8, stop:1 #e7c07f);"
            "  border: 2px solid #8a5a2a; border-radius: 10px;"
            "}"));
        m_peerImg = new QLabel(m_peerBar);
        m_peerImg->setFixedSize(48, 48);
        m_peerImg->setAlignment(Qt::AlignCenter);
        auto *peerName = new QLabel(QStringLiteral("奶娃帮手"), m_peerBar);
        QFont peerFont = peerName->font();
        peerFont.setBold(true);
        peerName->setFont(peerFont);
        m_peerMood = new QLabel(m_peerBar);
        m_peerMood->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_peerMovie = new QMovie(QStringLiteral(":/avatar/action.gif"), QByteArray(), m_peerBar);
        m_peerMovie->setCacheMode(QMovie::CacheAll);
        m_peerMovie->setScaledSize(QSize(48, 48));
        m_peerMovie2 = new QMovie(QStringLiteral(":/avatar/action2.gif"), QByteArray(), m_peerBar);
        m_peerMovie2->setCacheMode(QMovie::CacheAll);
        connect(m_peerMovie2, &QMovie::frameChanged, this, [this]() {
            if (m_peerMovie2->state() == QMovie::Running) {
                m_peerImg->setPixmap(m_peerMovie2->currentPixmap().scaled(
                    m_peerImg->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        });
        auto *peerLay = new QHBoxLayout(m_peerBar);
        peerLay->setContentsMargins(10, 4, 10, 4);
        peerLay->setSpacing(8);
        auto *peerText = new QVBoxLayout();
        peerText->setSpacing(0);
        peerText->addWidget(peerName);
        peerText->addWidget(m_peerMood);
        peerLay->addWidget(m_peerImg, 0, Qt::AlignVCenter);
        peerLay->addLayout(peerText, 1);
        m_peerBar->setFixedHeight(64);
        m_peerBar->setVisible(false);
        root->addWidget(m_peerBar);
    }

    root->addLayout(contentRow, 1);

    setCentralWidget(central);
#ifndef Q_OS_ANDROID
    resize(920, 700);   // 加宽以容纳第二行选项（背景/音乐/音量/执子）
#endif

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

    // 落子滑动动画驱动：60fps，播满 kMoveAnimFrames 帧自动停
    m_moveAnimTimer = new QTimer(this);
    m_moveAnimTimer->setInterval(16);
    connect(m_moveAnimTimer, &QTimer::timeout, this, [this] {
        if (++m_moveAnimFrame >= kMoveAnimFrames) {
            m_moveAnimTimer->stop();
            m_moveAnimActive = false;
        }
        if (m_view)
            m_view->update();
    });

    // AI 思考计时显示（AI 在后台线程，界面持续响应）
    m_thinkTimer = new QTimer(this);
    m_thinkTimer->setInterval(100);
    connect(m_thinkTimer, &QTimer::timeout, this, &MainWindow::onAiTick);
    m_thinkClock = new QElapsedTimer();

    // 挂机走子延时触发：单次定时器，start() 会重启已有计时，避免 singleShot 堆叠多个
    m_afkTimer = new QTimer(this);
    m_afkTimer->setSingleShot(true);
    connect(m_afkTimer, &QTimer::timeout, this, &MainWindow::startAfkMove);

    initSounds();   // 只创建音效对象；资源加载延后（见下）
    // 音频后端初始化（setSource）是同步耗时操作，9 个音效连着做会明显拖慢
    // "开始游戏"点击。挪到窗口显示后的第一轮事件循环，弹窗立刻、声音稍后到位。
    QTimer::singleShot(0, this, &MainWindow::loadSoundSources);
    loadCustomAssets();   // 恢复上次用过的自定义背景/音乐
    if (m_endgameIdx >= 0)
        initEndgame(m_endgameIdx);
    else
        initBoard();
    resetGameHash();
    resetKillerAndHistory();
    setAvatar(AvatarState::Idle);
    refreshButtons();

    // ===== 单机模式：应用开局前弹窗选好的难度/红黑方 =====
    if (m_mode == GameMode::SinglePlayer) {
        if (levelIdx >= 0 && levelIdx < m_levelCombo->count())
            m_levelCombo->setCurrentIndex(levelIdx);
        // 同步执子下拉框（会触发 onSideChanged，但 wantRed==m_playerRed 直接返回，安全）
        m_sideCombo->setCurrentIndex(m_playerRed ? 0 : 1);
        // 设置已在开局弹窗中确定，局内不再提供修改入口（隐藏下拉框和标签）
        m_levelCombo->hide();
        m_sideCombo->hide();
        const auto labels = findChildren<QLabel*>();
        for (QLabel *l : labels) {
            if (l->text().contains(QStringLiteral("难度")) || l->text().contains(QStringLiteral("执子")))
                l->hide();
        }
        statusBar()->showMessage(m_playerRed
            ? QStringLiteral("红方走棋（你执红先行）")
            : QStringLiteral("你执黑后手，糖蛋（红方）先走…"));
        // 残局：状态栏显示挑战目标与步数进度
        if (m_endgameIdx >= 0)
            updateEndgameStatus();
        // 玩家选后手：窗口显示后让 AI（红）先走
        if (!m_playerRed && m_endgameIdx < 0)
            QTimer::singleShot(0, this, [this]() { startAiThinking(); });
    }

    // ===== 联机模式专属设置 =====
    if (m_mode == GameMode::Online) {
        // 联机没有AI，把难度和执子选择藏起来
        m_levelCombo->hide();
        m_sideCombo->hide();
        // 找到对应的标签也隐藏（难度、执子）
        const auto labels = findChildren<QLabel*>();
        for (QLabel *l : labels) {
            if (l->text().contains(QStringLiteral("难度")) || l->text().contains(QStringLiteral("执子")))
                l->hide();
        }

        // 右侧聊天面板（停靠窗口，可拖动；不可关闭，防止关掉后没法聊天）
        auto *chatDock = new QDockWidget(QStringLiteral("房间聊天"), this);
        chatDock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
        chatDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
        auto *chatWidget = new QWidget(chatDock);
        auto *chatLayout = new QVBoxLayout(chatWidget);
        m_chatView = new QTextEdit(chatWidget);
        m_chatView->setReadOnly(true);
        m_chatInput = new QLineEdit(chatWidget);
        m_chatInput->setPlaceholderText(QStringLiteral("输入消息，回车发送…"));
        auto *sendBtn = new QPushButton(QStringLiteral("发送"), chatWidget);
        auto *inputRow = new QHBoxLayout();
        inputRow->addWidget(m_chatInput, 1);
        inputRow->addWidget(sendBtn);
        // 快捷表情按钮行
        auto *emojiRow = new QHBoxLayout();
        const QStringList emojis = {
            QStringLiteral("👍"), QStringLiteral("😂"), QStringLiteral("😮"),
            QStringLiteral("👏"), QStringLiteral("💪"), QStringLiteral("🤔"),
            QStringLiteral("🎉"), QStringLiteral("🙏")
        };
        for (const QString &emoji : emojis) {
            auto *emojiBtn = new QPushButton(emoji, chatWidget);
            emojiBtn->setFixedWidth(40);
            emojiBtn->setStyleSheet(QStringLiteral("QPushButton{font-size:18px;border:1px solid #ccc;border-radius:4px;padding:2px;}"));
            connect(emojiBtn, &QPushButton::clicked, this, [this, emoji]() {
                if (m_net) {
                    m_net->sendChat(emoji);
                    if (m_chatView)
                        m_chatView->append(QStringLiteral("<b style='color:#27ae60'>我：</b>") + emoji);
                }
            });
            emojiRow->addWidget(emojiBtn);
        }
        // 奶龙大表情（贴纸面板）
        auto *stickerBtn = new QPushButton(QStringLiteral("🐲"), chatWidget);
        stickerBtn->setFixedWidth(40);
        stickerBtn->setStyleSheet(QStringLiteral("QPushButton{font-size:18px;border:1px solid #ff922b;border-radius:4px;padding:2px;background:#fff4e6;}"));
        stickerBtn->setToolTip(QStringLiteral("奶龙表情"));
        connect(stickerBtn, &QPushButton::clicked, this, [this, stickerBtn]() {
            auto *picker = new StickerPicker(this);
            connect(picker, &StickerPicker::picked, this, [this](const QString &id) {
                if (!m_net) return;
                const QString token = QStringLiteral("[sticker:%1]").arg(id);
                m_net->sendChat(token);
                StickerPicker::appendMessage(
                    m_chatView, QStringLiteral("<b style='color:#27ae60'>我：</b>"), token);
            });
            connect(picker, &StickerPicker::emojiPicked, this, [this](const QString &e) {
                if (!m_net) return;
                m_net->sendChat(e);
                if (m_chatView)
                    m_chatView->append(QStringLiteral("<b style='color:#27ae60'>我：</b>") + e);
            });
            picker->exec();
            picker->deleteLater();
        });
        emojiRow->addWidget(stickerBtn);
        emojiRow->addStretch();
        chatLayout->addWidget(m_chatView, 1);
        chatLayout->addLayout(emojiRow);
        chatLayout->addLayout(inputRow);
        chatDock->setWidget(chatWidget);
        addDockWidget(Qt::RightDockWidgetArea, chatDock);

        connect(sendBtn, &QPushButton::clicked, this, &MainWindow::sendChatMessage);
        connect(m_chatInput, &QLineEdit::returnPressed, this, &MainWindow::sendChatMessage);

        if (m_summonBtn)
            connect(m_summonBtn, &QPushButton::clicked, this, &MainWindow::summonDragon);
        if (m_afkBtn)
            connect(m_afkBtn, &QPushButton::clicked, this, &MainWindow::toggleAfk);

        // 连接网络信号
        if (m_net) {
            connect(m_net, &NetworkManager::moveReceived, this, &MainWindow::onNetworkMoveReceived);
            connect(m_net, &NetworkManager::chatReceived, this, &MainWindow::onNetworkChatReceived);
            connect(m_net, &NetworkManager::avatarReceived, this, &MainWindow::onNetworkAvatar);
            connect(m_net, &NetworkManager::versionReceived, this, &MainWindow::onNetworkVersion);
            connect(m_net, &NetworkManager::undoRequested, this, &MainWindow::onNetworkUndoRequest);
            connect(m_net, &NetworkManager::undoAnswered, this, &MainWindow::onNetworkUndoAnswer);
            connect(m_net, &NetworkManager::restartRequested, this, &MainWindow::onNetworkRestartRequest);
            connect(m_net, &NetworkManager::restartAnswered, this, &MainWindow::onNetworkRestartAnswer);
            connect(m_net, &NetworkManager::badMoveReceived, this, &MainWindow::onNetworkBadMove);
            connect(m_net, &NetworkManager::resignReceived, this, &MainWindow::onNetworkResign);
            connect(m_net, &NetworkManager::disconnected, this, &MainWindow::onPeerDisconnected);
            connect(m_net, &NetworkManager::profileReceived,
                    this, &MainWindow::onNetworkProfile);
            connect(m_net, &NetworkManager::specChatReceived, this, &MainWindow::onSpecChatReceived);
            connect(m_net, &NetworkManager::gameEndReceived, this, &MainWindow::onGameEndReceived);
        }

        // 版本握手：双方窗口都建好后再互报版本（延迟一点，保证对方已挂上信号）
        // 观战者不发——服务器 version 属对局事件，观战者不在房间会弹"你不在任何房间中"
        if (!m_isSpectator) {
            QTimer::singleShot(800, this, [this]() {
                if (m_net)
                    m_net->sendVersion(QStringLiteral("3.3.0"));
            });
        }

        // 局域网名片握手：互报昵称/头衔/头像（互联网模式 sendProfile 内部为空操作，
        // 对手信息由 match_ready 携带）；观战者不发自己的名片
        if (!m_isSpectator) {
            QTimer::singleShot(300, this, [this]() {
                if (m_net && m_selfInfo.valid)
                    m_net->sendProfile(m_selfInfo.name, m_selfInfo.title,
                                       m_selfInfo.avatarUrl, m_playerRed);
            });
        }

        // 状态栏提示：观战/房主执红先走/加入者执黑等待
        if (m_isSpectator)
            statusBar()->showMessage(QStringLiteral("👁 观战模式：你只能观看，不能操作"));
        else
            statusBar()->showMessage(m_playerRed
                ? QStringLiteral("联机对战：你执红先行")
                : QStringLiteral("联机对战：你执黑，等待红方先走…"));
    }

    // ===== 名片初始填充 =====
    // 红方名片在棋盘下，黑方名片在棋盘上（与本机执子视角无关，双方看到的位置一致）
    if (m_isSpectator) {
        // 观战：构造参数 self=红方名片、opponent=黑方名片，立即填（红下黑上）
        applySideProfile(true, m_selfInfo);
        applySideProfile(false, m_oppInfo);
    } else if (m_mode == GameMode::SinglePlayer) {
        // 人机：自己 + 糖果AI（残局挑战无对面 AI，只显示自己，对面留占位）
        if (m_endgameIdx < 0) {
            PlayerCardInfo ai = m_oppInfo;
            if (!ai.valid) {
                static const char *kAiNames[] = {"笨糖果", "一般糖果", "聪明糖果"};
                const int idx = qBound(0, levelIdx, 2);
                ai.valid = true;
                ai.isAi = true;
                ai.name = QString::fromUtf8(kAiNames[idx]);
                ai.title = QStringLiteral("人工智能");
                ai.avatarSeed = 888 + idx;
            }
            // 本机视角：自己名片永远在棋盘下，对手在棋盘上
            fillCardPos(true, m_selfInfo, m_playerRed);
            fillCardPos(false, ai, !m_playerRed);
        } else {
            fillCardPos(true, m_selfInfo, m_playerRed);
        }
    } else {
        // 联机：自己名片永远在棋盘下；互联网对手 match_ready 已带，局域网对手等 PROFILE
        fillCardPos(true, m_selfInfo, m_playerRed);
        if (m_oppInfo.valid)
            fillCardPos(false, m_oppInfo, !m_playerRed);
    }
}

MainWindow::~MainWindow()
{
    if (m_openCounted)
        --s_openGameWindows;   // 正常在 closeEvent 里已减；未走 close 的删除路径兜底
    // 关窗口时 AI 还在算就等它结束，避免线程访问已销毁对象
    if (m_aiThread && m_aiThread->isRunning())
        m_aiThread->wait();
    delete m_thinkClock;
}

// ============================================================================
// 双方玩家名片（棋盘上=黑方，棋盘下=红方）
// ============================================================================
namespace {

// 头衔配色：全服第一金、全服第二蓝、全服最拉灰、AI 紫
QColor cardTitleColor(const QString &t)
{
    if (t == QStringLiteral("全服第一")) return QColor(212, 160, 23);
    if (t == QStringLiteral("全服第二")) return QColor(25, 118, 210);
    if (t == QStringLiteral("人工智能")) return QColor(126, 87, 194);
    return QColor(150, 150, 150);
}

} // namespace

QPixmap MainWindow::makeDefaultAvatar(const QString &name, int seed, int size)
{
    static const QColor palette[] = {
        QColor(255, 138, 128), QColor(129, 199, 132), QColor(129, 212, 250),
        QColor(206, 147, 216), QColor(255, 213, 79),  QColor(255, 171, 145),
        QColor(120, 204, 205), QColor(240, 152, 190)
    };
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(palette[qAbs(seed) % 8]);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(qMax(12, size * 18 / 36));
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter,
               name.left(1).toUpper());
    p.end();
    return out;
}

void MainWindow::buildPlayerCards(QWidget *parent, QVBoxLayout *boardCol)
{
#ifdef Q_OS_ANDROID
    const int avatarSize = 30;
    const int cardHeight = 42;
#else
    const int avatarSize = 40;
    const int cardHeight = 54;
#endif
    auto buildOne = [&](bool redSide) {
        auto *card = new QFrame(parent);
        card->setObjectName(redSide ? QStringLiteral("redSideCard")
                                    : QStringLiteral("blackSideCard"));
        card->setStyleSheet(QStringLiteral(
            "QFrame#redSideCard { background:#fdeeee; border:1px solid #e0a3a3;"
            "  border-radius:10px; }"
            "QFrame#blackSideCard { background:#eef1f5; border:1px solid #aab4c0;"
            "  border-radius:10px; }"));
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(10, 4, 10, 4);
        row->setSpacing(8);

        auto *avatar = new QLabel(card);
        avatar->setFixedSize(avatarSize, avatarSize);
        avatar->setAlignment(Qt::AlignCenter);

        auto *nameLbl = new QLabel(redSide ? QStringLiteral("红方玩家")
                                           : QStringLiteral("黑方玩家"), card);
        QFont nf = nameLbl->font();
        nf.setBold(true);
#ifdef Q_OS_ANDROID
        nf.setPixelSize(13);
#else
        nf.setPointSize(11);
#endif
        nameLbl->setFont(nf);

        auto *titleLbl = new QLabel(card);
        titleLbl->setStyleSheet(QStringLiteral("font-size:11px;"));

        auto *txt = new QVBoxLayout();
        txt->setSpacing(0);
        txt->addWidget(nameLbl);
        txt->addWidget(titleLbl);

        auto *sideTag = new QLabel(redSide ? QStringLiteral("红方")
                                           : QStringLiteral("黑方"), card);
        sideTag->setStyleSheet(redSide
            ? QStringLiteral("color:#c82622;font-weight:bold;")
            : QStringLiteral("color:#333;font-weight:bold;"));

        row->addWidget(avatar, 0, Qt::AlignVCenter);
        row->addLayout(txt, 1);
        row->addWidget(sideTag, 0, Qt::AlignVCenter);
        card->setFixedHeight(cardHeight);
        boardCol->addWidget(card);

        if (redSide) {
            m_cardBottom = card;
            m_botAvatar = avatar; m_botName = nameLbl; m_botTitle = titleLbl;
            m_botTag = sideTag;
        } else {
            m_cardTop = card;
            m_topAvatar = avatar; m_topName = nameLbl; m_topTitle = titleLbl;
            m_topTag = sideTag;
        }
        // 初始占位头像
        avatar->setPixmap(makeDefaultAvatar(
            redSide ? QStringLiteral("红") : QStringLiteral("黑"),
            redSide ? 1 : 2, avatarSize));
    };
    buildOne(false);   // 上方黑方
    buildOne(true);    // 下方红方

    m_cardNam = new QNetworkAccessManager(this);
}

void MainWindow::applySideProfile(bool redSide, const PlayerCardInfo &info)
{
    if (!info.valid)
        return;
    fillCard(redSide, info);
}

void MainWindow::fillCard(bool redSide, const PlayerCardInfo &info)
{
    QLabel *nameLbl = redSide ? m_botName : m_topName;
    QLabel *titleLbl = redSide ? m_botTitle : m_topTitle;
    QLabel *avatarLbl = redSide ? m_botAvatar : m_topAvatar;
    if (!nameLbl)
        return;

    QString shown = info.name.isEmpty()
        ? (redSide ? QStringLiteral("红方玩家") : QStringLiteral("黑方玩家"))
        : info.name;
    if (info.isSelf)
        shown += QStringLiteral("（你）");
    nameLbl->setText(shown);

    const QString t = info.title;
    titleLbl->setText(t.isEmpty() ? QString() : QStringLiteral("◇ %1").arg(t));
    const QColor c = cardTitleColor(t);
    titleLbl->setStyleSheet(
        QStringLiteral("font-size:11px;color:rgb(%1,%2,%3);")
            .arg(c.red()).arg(c.green()).arg(c.blue()));

    loadCardAvatar(avatarLbl, info.avatarUrl,
                   info.name.isEmpty()
                       ? (redSide ? QStringLiteral("红") : QStringLiteral("黑"))
                       : info.name,
                   info.avatarSeed ? info.avatarSeed
                                   : (redSide ? 1 : 2));
}

// 按位置填名片：本机视角自己永远在棋盘下；卡片底色/阵营标按内容实际执子方重着色
void MainWindow::fillCardPos(bool bottom, const PlayerCardInfo &info, bool infoRed)
{
    if (!info.valid)
        return;
    QFrame *card = bottom ? m_cardBottom : m_cardTop;
    QLabel *tag = bottom ? m_botTag : m_topTag;
    if (card) {
        card->setStyleSheet(infoRed
            ? QStringLiteral("QFrame { background:#fdeeee; border:1px solid #e0a3a3;"
                             "  border-radius:10px; }")
            : QStringLiteral("QFrame { background:#eef1f5; border:1px solid #aab4c0;"
                             "  border-radius:10px; }"));
    }
    if (tag) {
        tag->setText(infoRed ? QStringLiteral("红方") : QStringLiteral("黑方"));
        tag->setStyleSheet(infoRed
            ? QStringLiteral("color:#c82622;font-weight:bold;")
            : QStringLiteral("color:#333;font-weight:bold;"));
    }
    fillCard(bottom, info);   // fillCard 的 redSide 实参仅用于选上下排控件
}

void MainWindow::loadCardAvatar(QLabel *lbl, const QString &url,
                                const QString &name, int seed)
{
#ifdef Q_OS_ANDROID
    const int sz = 30;
#else
    const int sz = 40;
#endif
    // 无自定义头像：直接画彩色默认头像
    if (url.isEmpty() || !ServerConfig::isConfigured()) {
        lbl->setPixmap(makeDefaultAvatar(name, seed, sz));
        return;
    }
    // 先放默认头像占位，下载成功再替换
    lbl->setPixmap(makeDefaultAvatar(name, seed, sz));

    QNetworkRequest req((QUrl(ServerConfig::serverUrl() + url)));
    QNetworkReply *reply = m_cardNam->get(req);
    QPointer<QLabel> weak(lbl);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, weak, sz]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || !weak)
            return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll()))
            return;
        QPixmap scaled = pm.scaled(sz, sz, Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
        const int x = (scaled.width() - sz) / 2;
        const int y = (scaled.height() - sz) / 2;
        const QPixmap cropped = scaled.copy(x, y, sz, sz);
        QPixmap out(sz, sz);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addEllipse(0, 0, sz, sz);
        p.setClipPath(path);
        p.drawPixmap(0, 0, cropped);
        p.end();
        weak->setPixmap(out);
    });
}

// 局域网收到对方名片：senderRed=发送方执红（观战者也会分别收到双方）
void MainWindow::onNetworkProfile(const QString &name, const QString &title,
                                  const QString &avatarUrl, bool senderRed)
{
    PlayerCardInfo info;
    info.valid = true;
    info.name = name;
    info.title = title;
    info.avatarUrl = avatarUrl;
    info.avatarSeed = qHash(name) & 0x7fffffff;
    // 观战保持红下黑上；对局者视角对方永远在棋盘上
    if (m_isSpectator)
        applySideProfile(senderRed, info);
    else
        fillCardPos(false, info, senderRed);
}

// 关闭对局窗口：互联网模式通知服务器离开房间（对方会收到断线提示并释放房间）
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_mode == GameMode::Online && m_net) {
        if (m_isSpectator)
            m_net->sendSpectateLeave();   // 观战：退出观战频道
        else
            m_net->leaveRoom();
    }
    QMainWindow::closeEvent(event);
    // 窗口只关不删（对象由 StartScreen 持有）：计数必须在这里减，否则对局判定卡死
    if (m_openCounted) {
        --s_openGameWindows;
        m_openCounted = false;
    }
    // 机器人：进程直接退出，由编排脚本重启进入下一局
    // （机器人双开共享注册表，重复 new StartScreen 会串登录态，直接退出最干净）
    if (m_botPlay) {
        qApp->exit(0);
        return;
    }
    // 程序内部清理（onInternetMatched/onSpectateStarted 收摊旧窗）：不拉起新主菜单，
    // 否则 closeEvent 里 new StartScreen 会 delete 掉正在堆栈上执行的主菜单实例（野指针崩溃）
    if (m_silentClose)
        return;
    // 回主菜单：复用常驻的 StartScreen 实例，而不是 new 新的
    // （new 会触发构造函数 delete 旧实例，若旧实例在堆栈上执行就是野指针崩溃）
    if (StartScreen::s_liveMenu) {
        StartScreen::s_liveMenu->show();
        StartScreen::s_liveMenu->raise();
        StartScreen::s_liveMenu->activateWindow();
    } else {
        // 兜底：主菜单不存在时才创建（理论上不应该发生）
        auto *menu = new StartScreen;
        menu->show();
    }
}

// 返回主菜单：closeEvent 会重新拉起主菜单；联机对局未结束先确认，防误触断线
void MainWindow::goBackToMenu()
{
    if (m_mode == GameMode::Online && !m_gameOver) {
        auto ret = QMessageBox::question(
            this, QStringLiteral("返回主菜单"),
            m_isSpectator
                ? QStringLiteral("确定要离开房间吗？")
                : QStringLiteral("对局尚未结束，确定要返回主菜单吗？\n离开后将断开与对方的连接。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }
    close();
}

// 懒加载背景音乐播放器：真正的创建延后到窗口显示后（见构造函数）
void MainWindow::ensureMusicPlayer()
{
    if (m_musicPlayer)
        return;
    m_audioOut = new QAudioOutput(this);
    m_audioOut->setVolume(m_volSlider ? m_volSlider->value() / 100.0f : 0.5f);
    // 机器人后台运行：背景音乐输出整体静音（双保险，bot 本无音乐交互）
    if (m_botPlay)
        m_audioOut->setMuted(true);
    m_musicPlayer = new QMediaPlayer(this);
    m_musicPlayer->setAudioOutput(m_audioOut);
    m_musicPlayer->setLoops(QMediaPlayer::Infinite);
}

// 预加载音效：wav 已编进 qrc，程序启动时一次性载入内存，点击即播无延迟
void MainWindow::initSounds()
{
    // 只创建对象并设音量；setSource（真正触发音频后端初始化，较慢）
    // 统一延后到 loadSoundSources()，避免卡住"开始游戏"的点击响应
    auto mk = [this](float vol) {
        auto *s = new QSoundEffect(this);
        s->setVolume(vol);
        return s;
    };
    m_sndSelect     = mk(0.5f);
    m_sndMove       = mk(0.6f);
    m_sndCapture    = mk(0.6f);   // 黑方/对面吃子（视频提取）
    m_sndCapture2   = mk(0.8f);   // 挂机奶娃吃子随机音效（视频2提取）
    m_sndCaptureRed = mk(0.6f);   // 红方/我方吃子（原版合成）
    m_sndCheck      = mk(0.6f);   // 黑方/对面将军（视频提取）
    m_sndCheckRed   = mk(0.6f);   // 红方/我方将军（原版合成）
    m_sndWin        = mk(0.6f);
    m_sndLose       = mk(0.6f);   // 黑方胜利/投降大笑（视频提取）
}

// 给音效绑定资源：在窗口显示后的第一轮事件循环里执行
// （玩家最快也要几百毫秒后才落第一子，此时早已加载完毕，听感无差异）
void MainWindow::loadSoundSources()
{
    // 机器人后台运行不外放声音：不绑定资源，play() 变空操作
    if (m_botPlay)
        return;
    m_sndSelect->setSource(QUrl(QStringLiteral("qrc:/sounds/select.wav")));
    m_sndMove->setSource(QUrl(QStringLiteral("qrc:/sounds/move.wav")));
    m_sndCapture->setSource(QUrl(QStringLiteral("qrc:/sounds/capture.wav")));
    m_sndCapture2->setSource(QUrl(QStringLiteral("qrc:/sounds/capture2.wav")));
    m_sndCaptureRed->setSource(QUrl(QStringLiteral("qrc:/sounds/capture_red.wav")));
    m_sndCheck->setSource(QUrl(QStringLiteral("qrc:/sounds/check.wav")));
    m_sndCheckRed->setSource(QUrl(QStringLiteral("qrc:/sounds/check_red.wav")));
    m_sndWin->setSource(QUrl(QStringLiteral("qrc:/sounds/win.wav")));
    m_sndLose->setSource(QUrl(QStringLiteral("qrc:/sounds/lose.wav")));
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

    // 挂机奶娃吃子时随机播放的新版出拳动作（240x240，比头像框大，
    // 不能直接 setMovie，改为监听帧变化、缩放后用 pixmap 上屏）
    m_avatarMovie2 = new QMovie(QStringLiteral(":/avatar/action2.gif"), QByteArray(), this);
    m_avatarMovie2->setCacheMode(QMovie::CacheAll);
    connect(m_avatarMovie2, &QMovie::frameChanged, this, [this]() {
        if (m_avatarMovie2->state() == QMovie::Running) {
            m_avatarImg->setPixmap(m_avatarMovie2->currentPixmap().scaled(
                m_avatarImg->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    });

    m_avatarTimer = new QTimer(this);
    m_avatarTimer->setSingleShot(true);
    connect(m_avatarTimer, &QTimer::timeout, this, &MainWindow::onAvatarTimer);
}

// 切换形象：静态图直接换 pixmap；Action 状态播 gif
// 音乐开启时锁定为听歌脸，拒绝任何切换（但 Listening 自己放行）
// 联机加入者：形象条完全由房主端同步驱动，本地事件不改变它
void MainWindow::setAvatar(AvatarState s)
{
    if (m_mode == GameMode::Online && !m_isHost)
        return;
    if (m_musicOn && s != AvatarState::Listening)
        s = AvatarState::Listening;
    m_avatarState = s;
    syncAvatar(stateKey(s));   // 房主：把奶娃状态广播给对面（其余情况为空操作）

    if (s == AvatarState::Action && m_avatarMovie->isValid()) {
        if (m_avatarMovie2->state() == QMovie::Running)
            m_avatarMovie2->stop();
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
    if (m_avatarMovie2 && m_avatarMovie2->state() == QMovie::Running)
        m_avatarMovie2->stop();
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
    const bool online = (m_mode == GameMode::Online);
    // 单机：悔棋需有历史、重开无限制；联机：悔棋/重开/投降都随时可点（消息发出去等对面确认）
    m_undoBtn->setEnabled(!m_aiThinking && !m_waitingUndo
                          && (online ? true : !m_history.empty()));
    m_restartBtn->setEnabled(!m_aiThinking && !m_waitingRestart);
    // 单机模式：对局进行中锁定难度/阵营，终局后解锁
    const bool controlsLocked = online || (m_aiThinking && !m_gameOver) || (!m_gameOver && !m_history.empty());
    m_levelCombo->setEnabled(!controlsLocked);
    m_sideCombo->setEnabled(!controlsLocked);
    m_resignBtn->setEnabled(!m_gameOver && !m_aiThinking);
    refreshCandyButton();   // 糖果按钮跟随回合/思考状态刷新
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

    // 联机：不直接重开，发请求等对方同意（保证两边棋盘同步重置）
    if (m_mode == GameMode::Online) {
        sendRestartRequest();
        return;
    }

    if (m_endgameIdx >= 0)
        initEndgame(m_endgameIdx);
    else
        initBoard();
    resetGameHash();
    resetKillerAndHistory();
    m_history.clear();
    m_endgameMoves = 0;   // 残局：重开重置步数统计
    clearSelection();
    m_hasLast = false;
    m_moveAnimActive = false;   // 清掉进行中的落子动画，避免新局残留滑动棋子
    m_moveAnimTimer->stop();
    m_gameOver = false;
    m_moneySettled = false;   // 新一局：允许再次结算货币
    m_aiThinking = false;
    m_animPhase = 0.0;
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    m_avatarTimer->stop();
    setAvatar(AvatarState::Idle);
    updateCheckState();
    updateEndgameStatus();
    refreshButtons();
    m_view->update();
}

void MainWindow::undoMove()
{
    if (m_history.empty() || m_aiThinking)
        return;

    // 联机：不直接悔棋，发请求等对方同意（保证两边棋盘同步回退）
    if (m_mode == GameMode::Online) {
        sendUndoRequest();
        return;
    }

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
        // 残局：悔棋回退玩家步数统计
        if (m_endgameIdx >= 0 && m_endgameMoves > 0)
            m_endgameMoves--;
    }

    m_gameOver = false;
    clearSelection();
    m_moveAnimActive = false;   // 悔棋后棋盘回退，停掉滑动动画
    m_moveAnimTimer->stop();
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

// 货币结算：仅单机模式，每局只发一次；返回发放金额（0=不适用/已发过）
int MainWindow::settleMoney(int outcome)
{
    if (m_mode != GameMode::SinglePlayer || m_moneySettled)
        return 0;
    m_moneySettled = true;
    const int award = (outcome == 1) ? 2000 : (outcome == 2) ? 1000 : 500;
    QSettings st;
    const int cur = st.value(QStringLiteral("money"), 0).toInt();
    st.setValue(QStringLiteral("money"), cur + award);
    return award;
}

// 残局挑战：状态栏显示进度（名称/目标/已走步数）
void MainWindow::updateEndgameStatus()
{
    if (m_endgameIdx < 0)
        return;
    const EndgameInfo &info = endgameInfo(m_endgameIdx);
    statusBar()->showMessage(
        QStringLiteral("🏁 残局挑战《%1》· %2（已走 %3/%4 步）")
            .arg(QString::fromUtf8(info.name), QString::fromUtf8(info.goal))
            .arg(m_endgameMoves).arg(info.moveLimit));
}

// 残局失败后的统一询问：再试一次（重开本残局）或返回主菜单
void MainWindow::endgameMaybeRetry(const QString &reason)
{
    if (m_endgameIdx < 0)
        return;
    const QString name = QString::fromUtf8(endgameInfo(m_endgameIdx).name);
    const auto ret = QMessageBox::question(this, QStringLiteral("残局挑战"),
        QStringLiteral("%1\n\n《%2》挑战失败，是否再试一次？").arg(reason, name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (ret == QMessageBox::Yes)
        restartGame();
    else
        close();
}

// 胜利成就：残局通关/击败奶娃(人机)/互联网取胜，外加杀法称号；解锁弹窗
void MainWindow::reportWinAchievements(const QString &killName)
{
    if (m_botPlay)
        return;   // 机器人不打成就、不弹解锁窗
    QStringList notices;
    if (m_endgameIdx >= 0) {
        notices += Achievements::recordEvent(QStringLiteral("endgame"), 1);
    } else if (m_mode == GameMode::SinglePlayer) {
        notices += Achievements::recordEvent(QStringLiteral("aiWin"), 1);
    } else if (m_net && m_net->transport() == NetworkManager::Transport::Internet) {
        notices += Achievements::recordEvent(QStringLiteral("onlineWin"), 1);
    }
    // 杀法称号：人机/局域网/互联网打出都算
    const QString killStat = Achievements::killStatKey(killName);
    if (!killStat.isEmpty())
        notices += Achievements::recordEvent(killStat, 1);
    if (!notices.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("🏆 成就解锁"),
            notices.join(QStringLiteral("\n\n")));
    }
}

// ===== 糖果指导（消耗品）：单机/局域网房客/互联网可用，房主和观战不可用 =====
bool MainWindow::candyAllowed() const
{
    if (m_isSpectator)
        return false;
    if (m_mode == GameMode::SinglePlayer)
        return true;
    // 联机：局域网仅房客可用（房主有奶娃挂机，不再给糖果）；互联网双方可用
    if (m_net && m_net->transport() == NetworkManager::Transport::Lan)
        return !m_isHost;
    return true;
}

int MainWindow::candyCount(int idx) const
{
    return QSettings().value(QStringLiteral("candy%1").arg(idx), 0).toInt();
}

// 糖果名/emoji/思考时间（与商城一致）
static const struct { const char *emoji; const char *name; int ms; } kCandyDefs[] = {
    { "🍭", "笨糖果",   10000 },
    { "🍬", "一般糖果", 15000 },
    { "🍫", "聪明糖果", 20000 },
};

// 刷新糖果按钮：文字带剩余总数，只在轮到我走且未终局时可用
void MainWindow::refreshCandyButton()
{
    if (!m_candyBtn)
        return;
    const int total = candyCount(0) + candyCount(1) + candyCount(2);
    m_candyBtn->setText(QStringLiteral("🍬 糖果指导 x%1").arg(total));
    const bool myTurn = (m_mode == GameMode::SinglePlayer) ? true : m_myTurn;
    m_candyBtn->setEnabled(total > 0 && !m_gameOver && !m_aiThinking && myTurn);
}

// 点击糖果按钮：有库存的糖果弹菜单选一颗
void MainWindow::useCandy()
{
    if (m_gameOver || m_aiThinking)
        return;
    if (m_mode == GameMode::Online && !m_myTurn) {
        statusBar()->showMessage(QStringLiteral("还没轮到你走"));
        return;
    }
    QMenu menu(this);
    QList<QPair<int, QAction*>> acts;
    for (int i = 0; i < 3; i++) {
        const int n = candyCount(i);
        if (n <= 0)
            continue;
        QAction *a = menu.addAction(QStringLiteral("%1 %2（思考 %3 秒）剩余 x%4")
            .arg(QString::fromUtf8(kCandyDefs[i].emoji))
            .arg(QString::fromUtf8(kCandyDefs[i].name))
            .arg(kCandyDefs[i].ms / 1000)
            .arg(n));
        acts.append({ i, a });
    }
    if (acts.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("没有糖果"),
            QStringLiteral("糖果用完啦，去商城买一些吧！"));
        return;
    }
    QAction *picked = menu.exec(QCursor::pos());
    if (!picked)
        return;
    for (const auto &p : acts) {
        if (p.second == picked) {
            // 消耗一颗并启动代走
            QSettings st;
            const QString key = QStringLiteral("candy%1").arg(p.first);
            st.setValue(key, st.value(key, 0).toInt() - 1);
            startCandyMove(p.first);
            break;
        }
    }
}

// 消耗糖果后：AI 按糖果时长替我思考（执我方颜色）
void MainWindow::startCandyMove(int idx)
{
    if (m_gameOver || m_aiThinking)
        return;
    m_aiThinking = true;
    refreshButtons();
    refreshCandyButton();
    statusBar()->showMessage(QStringLiteral("%1 思考中…")
        .arg(QString::fromUtf8(kCandyDefs[idx].name)));

    m_aiThread = new AiThread();
    std::memcpy(m_aiThread->m_board, coreBoard, sizeof(coreBoard));
    m_aiThread->m_timeMs = kCandyDefs[idx].ms;
    m_aiThread->m_redSide = m_playerRed;   // 替我方想
    connect(m_aiThread, &QThread::finished, this, &MainWindow::onCandyFinished);
    connect(m_aiThread, &QThread::finished, m_aiThread, &QObject::deleteLater);

    m_thinkClock->restart();
    m_thinkTimer->start();
    onAiTick();
    m_aiThread->start();
}

// 糖果 AI 搜索结束：代我落子 → 判重复 → 判终局 →（联机发网/单机触发对面AI）
void MainWindow::onCandyFinished()
{
    const Move mv = m_aiThread->m_result;
    m_aiThread = nullptr;
    m_thinkTimer->stop();
    m_aiThinking = false;

    // 兜底：我方无棋可走 = 我方输
    if (!(inBoard(mv.x1, mv.y1) && inBoard(mv.x2, mv.y2))) {
        m_gameOver = true;
        m_animTimer->stop();
        statusBar()->showMessage(QStringLiteral("你方无棋可走，对方胜利"));
        if (m_mode == GameMode::SinglePlayer) {
            setAvatar(AvatarState::Win);
            const int award = settleMoney(0);
            m_view->update();
            QMessageBox::information(this, QStringLiteral("对局结束"),
                QStringLiteral("对面胜利！\n你方无子可动（困毙）")
                    + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
        } else {
            m_sndLose->play();
            m_view->update();
            QMessageBox::information(this, QStringLiteral("对局结束"),
                QStringLiteral("对方胜利！\n你方无子可动（困毙）"));
        }
        endgameMaybeRetry(QStringLiteral("你方无子可动（困毙）"));
        refreshButtons();
        refreshCandyButton();
        return;
    }

    // ---- 代走落子，记入历史 ----
    Step s;
    s.captured = coreBoard[mv.x2][mv.y2];
    s.piece = coreBoard[mv.x1][mv.y1];
    executeMove(mv);
    s.move = mv;
    s.byRed = m_playerRed;
    m_history.push_back(s);
    m_last = mv;
    m_hasLast = true;
    animateMove(mv);   // 落子动画：代走这步滑动

    // ---- 长将/重复局面检测：代走犯规算我方输 ----
    {
        const uint64_t newHash = getBoardHash(coreBoard, m_playerRed);
        if (isGameRepetition(newHash)) {
            m_gameOver = true;
            statusBar()->showMessage(QStringLiteral("长将犯规 —— 你输了！"));
            pushGameHash(newHash);
            m_view->update();
            const int award = settleMoney(0);
            QMessageBox::information(this, QStringLiteral("对局结束"),
                QStringLiteral("长将犯规，你输了！\n（连续重复局面自动判负）")
                    + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
            endgameMaybeRetry(QStringLiteral("长将犯规"));
            refreshButtons();
            refreshCandyButton();
            return;
        }
        pushGameHash(newHash);
    }

    // ---- 音效：我方吃子用红方音效 ----
    if (s.captured != 0)
        m_sndCaptureRed->play();
    else
        m_sndMove->play();
    clearSelection();

    // ---- 判对面终局 ----
    const int oppState = checkGameOver(coreBoard, !m_playerRed);
    if (oppState != 0)
        m_gameOver = true;
    updateCheckState();
    m_view->update();

    // ---- 联机：走法必须在弹任何终局对话框之前发出（模态对话框会阻塞事件循环）----
    if (m_mode == GameMode::Online) {
        m_myTurn = false;
        if (m_net)
            m_net->sendMove(mv.x1, mv.y1, mv.x2, mv.y2);
    }

    if (oppState != 0) {
        const QString kill = QString::fromLocal8Bit(
            getKillTypeName(coreBoard, !m_playerRed).c_str());
        const QString reason = (oppState == 1)
            ? QStringLiteral("对面被将死")
            : QStringLiteral("对面无子可动（困毙）");
        statusBar()->showMessage(QStringLiteral("糖果指导帮你赢了！"));
        m_animTimer->stop();
        m_avatarTimer->stop();
        if (m_mode == GameMode::SinglePlayer)
            setAvatar(AvatarState::Lose);
        m_sndWin->play();
        m_view->update();
        const int award = settleMoney(1);
        QString winMsg;
        if (m_endgameIdx >= 0) {
            const EndgameInfo &info = endgameInfo(m_endgameIdx);
            winMsg = QStringLiteral("🏁 残局挑战成功！\n《%1》· 用 %2 步完成（上限 %3 步）\n杀法：%4")
                         .arg(QString::fromUtf8(info.name)).arg(m_endgameMoves)
                         .arg(info.moveLimit).arg(kill);
        } else {
            winMsg = QStringLiteral("🎉 你赢了！\n%1\n杀法：%2").arg(reason, kill);
        }
        QMessageBox::information(this, QStringLiteral("对局结束"), winMsg
            + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
        reportWinAchievements(kill);
    } else if (m_mode == GameMode::SinglePlayer) {
        // 单机：我走完轮到 AI 对手思考
        statusBar()->showMessage(QStringLiteral("糖果指导已落子，糖蛋思考中…"));
        startAiThinking();
    } else {
        statusBar()->showMessage(QStringLiteral("糖果指导已落子，等待对方…"));
    }
    refreshButtons();
    refreshCandyButton();
}

// 投降：直接判负，对面大笑（咧嘴笑 + 视频笑声）
void MainWindow::resignGame()
{
    if (m_gameOver || m_aiThinking)
        return;

    m_gameOver = true;
    m_resultReported = true;  // 投降服务器已直接判负，不再发 game_end
    // 联机：通知对方投降（互联网由服务器落库，局域网转发 RESIGN 文本）
    if (m_mode == GameMode::Online && m_net)
        m_net->sendResign();
    clearSelection();
    m_animTimer->stop();
    m_avatarTimer->stop();
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    const QString winner = m_playerRed ? QStringLiteral("黑方") : QStringLiteral("红方");
    const int award = settleMoney(0);   // 单机投降：+500￥
    statusBar()->showMessage(award > 0
        ? QStringLiteral("你投降了 —— %1胜利！💰 +%2￥").arg(winner).arg(award)
        : QStringLiteral("你投降了 —— %1胜利！").arg(winner));
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

// 把用户选择的文件复制到应用私有 assets/ 目录（防止源文件被移动/删除后失效）
// 桌面版放在 exe 旁；Android 上 APK 安装目录只读，改用 AppDataLocation
QString MainWindow::copyToAssets(const QString &src, const QString &tag)
{
#ifdef Q_OS_ANDROID
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    const QString base = QCoreApplication::applicationDirPath();
#endif
    QDir dir(base + QStringLiteral("/assets"));
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
    ensureMusicPlayer();   // 播放器懒加载兜底（正常已在窗口显示后建好）
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
    // 残局：黑方（防守方）固定 3 秒浅算，让玩家更容易完成杀局
    m_aiThread->m_timeMs = (m_endgameIdx >= 0) ? 3000
                                               : m_levelCombo->currentData().toInt();
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
    if (m_afk) {
        statusBar()->showMessage(QStringLiteral("🐲 奶娃思考中… %1 秒").arg(sec, 0, 'f', 1));
        return;
    }
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
        const int award = settleMoney(1);
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("糖蛋赢了，敢不敢挑战更聪明的我？")
                + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
        reportWinAchievements(QString());
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
    animateMove(ai);   // 落子动画：AI 这步滑动
    // AI 落子：对面吃子用对面音效，普通走动用木质落子声
    (aiStep.captured != 0 ? m_sndCapture : m_sndMove)->play();

    // 长将/重复局面检测：AI 走子后导致重复局面 → AI 判负（电脑先妥协）
    const uint64_t newHash = getBoardHash(coreBoard, !m_playerRed);
    if (isGameRepetition(newHash)) {
        m_gameOver = true;
        statusBar()->showMessage(QStringLiteral("电脑长将犯规 —— 你赢了！"));
        setAvatar(AvatarState::Lose);
        m_sndWin->play();
        pushGameHash(newHash);
        m_view->update();
        const int award = settleMoney(1);
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("电脑长将犯规，你赢了！\n（连续重复局面自动判负）")
                + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
        reportWinAchievements(QString());
        refreshButtons();
        return;
    }
    pushGameHash(newHash);

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
        const int award = settleMoney(0);
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("对面胜利！\n%1\n杀法：%2").arg(reason, kill)
                + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
        endgameMaybeRetry(reason);
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
    // 观战模式：完全锁定棋盘
    if (m_isSpectator)
        return;
    // 联机模式：不是我的回合，棋盘锁定（右键仍可取消选中）
    if (m_mode == GameMode::Online && !m_myTurn && !rightButton)
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
            // 起点坐标先存局部：后面 clearSelection() 会把 m_selRow/m_selCol 清成 -1，
            // 联机发送必须用这里保存的合法坐标（修复：手动走棋发 MOVE -1,-1 的 bug）
            const int fromRow = m_selRow, fromCol = m_selCol;

            // 尝试走子（movePiece 内部校验走法、将帅对脸、送将）
            if (!movePiece(m_selRow, m_selCol, row, col)) {
                m_view->update();
                return;
            }

            // ---- 玩家落子成立，记入历史 ----
            Step playerStep;
            playerStep.move = Move(fromRow, fromCol, row, col);
            playerStep.piece = coreBoard[row][col];
            playerStep.captured = captured;
            playerStep.byRed = m_playerRed;
            m_history.push_back(playerStep);
            m_last = playerStep.move;
            m_hasLast = true;
            animateMove(playerStep.move);   // 落子动画：玩家这步从起点滑向终点
            // 残局：统计玩家步数（超上限判负）
            if (m_endgameIdx >= 0) {
                m_endgameMoves++;
                updateEndgameStatus();
            }
            // 落子音：我方吃子用我方音效，普通走动用木质落子声
            (captured != 0 ? m_sndCaptureRed : m_sndMove)->play();
            // 吃掉 AI 棋子：对面形象先做愤怒脸 -> gif 动作（与吃子音同步）
            if (captured != 0)
                playCaptureReaction();
            clearSelection();
            refreshButtons();

            // 长将/重复局面检测：玩家走子后导致重复局面 → 玩家判负
            {
                const uint64_t newHash = getBoardHash(coreBoard, m_playerRed);
                if (isGameRepetition(newHash)) {
                    m_gameOver = true;
                    statusBar()->showMessage(QStringLiteral("你长将犯规 —— 你输了！"));
                    setAvatar(AvatarState::Win);
                    m_sndLose->play();
                    pushGameHash(newHash);
                    m_view->update();
                    // 联机：先发送走步再弹窗
                    if (m_mode == GameMode::Online && m_net) {
                        m_myTurn = false;
                        m_net->sendMove(fromRow, fromCol, row, col);
                    }
                    const int award = settleMoney(0);
                    QMessageBox::information(this,
                        QStringLiteral("对局结束"),
                        QStringLiteral("你长将犯规，输了！\n（连续重复局面自动判负）")
                            + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
                    if (m_endgameIdx >= 0)
                        endgameMaybeRetry(QStringLiteral("长将犯规"));
                    refreshButtons();
                    // 解锁难度/阵营选择
                    if (m_mode == GameMode::SinglePlayer) {
                        m_levelCombo->setEnabled(true);
                        m_sideCombo->setEnabled(true);
                    }
                    return;
                }
                pushGameHash(newHash);
            }

            // ---- 玩家走完，轮到 AI：先判终局（将死则只放胜利音，不再响将军警报）----
            const bool aiRed = !m_playerRed;
            const int aiState = checkGameOver(coreBoard, aiRed);
            if (aiState != 0)
                m_gameOver = true;
            updateCheckState();
            m_view->update();

            // ---- 联机：无论这步是否终局都必须先发给对方——
            // 若将死/困毙后才 return，消息永远发不出，对方棋盘会卡死等这步 ----
            if (m_mode == GameMode::Online) {
                m_myTurn = false;
                if (m_net)
                    m_net->sendMove(fromRow, fromCol, row, col);
                statusBar()->showMessage(QStringLiteral("已走子，等待对方…"));
            }

            if (aiState != 0) {
                // 互联网：上报终局（我方颜色获胜），服务器只认第一份，重复无害
                if (m_mode == GameMode::Online && m_net && !m_resultReported) {
                    m_resultReported = true;
                    m_net->sendGameEnd(m_playerRed
                        ? QStringLiteral("red_win") : QStringLiteral("black_win"));
                }
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
                const int award = settleMoney(1);
                QString winMsg;
                if (m_endgameIdx >= 0) {
                    const EndgameInfo &info = endgameInfo(m_endgameIdx);
                    winMsg = QStringLiteral("🏁 残局挑战成功！\n《%1》· 用 %2 步完成（上限 %3 步）\n杀法：%4")
                                 .arg(QString::fromUtf8(info.name)).arg(m_endgameMoves)
                                 .arg(info.moveLimit).arg(kill);
                } else {
                    winMsg = QStringLiteral("🎉 你赢了！\n%1\n杀法：%2").arg(reason, kill);
                }
                QMessageBox::information(this,
                    QStringLiteral("对局结束"), winMsg
                        + (award > 0 ? QStringLiteral("\n💰 +%1￥").arg(award) : QString()));
                reportWinAchievements(kill);
                refreshButtons();
                return;
            }

            // 残局：超过步数上限判负（此步已将杀黑方则上面已 return）
            if (m_endgameIdx >= 0 && m_endgameMoves > endgameInfo(m_endgameIdx).moveLimit) {
                m_gameOver = true;
                statusBar()->showMessage(QStringLiteral("⏱ 挑战失败：步数用尽"));
                m_view->update();
                QMessageBox::information(this, QStringLiteral("残局挑战"),
                    QStringLiteral("⏱ 挑战失败\n《%1》超过 %2 步未能将杀黑将")
                        .arg(QString::fromUtf8(endgameInfo(m_endgameIdx).name))
                        .arg(endgameInfo(m_endgameIdx).moveLimit));
                endgameMaybeRetry(QStringLiteral("步数用尽"));
                return;
            }

            if (m_mode == GameMode::Online) {
                // 走子已发，等对方走（m_myTurn 已置 false）
            } else {
                // ---- 单机：AI 后台应招（按难度 10/15/20 秒）----
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
        // 必须明显小于棋子半径(0.43)：终点圆点被棋子完全盖住，
        // 否则圆边露出会像棋子的"阴影/残影"
        const qreal hl = g.cell * 0.30;
        p.drawEllipse(dp(m_last.x1, m_last.y1), hl, hl);
        p.drawEllipse(dp(m_last.x2, m_last.y2), hl, hl);
    }

    // ---- 8. 棋子 ----
    const qreal radius = g.cell * 0.43;
    // 字体缓存：cell 尺寸不变时复用，避免 60fps 动画时每次创建 QFont
    const int cellPx = int(g.cell);
    if (m_pieceFontCell != cellPx) {
        m_pieceFont = QFont(QStringLiteral("KaiTi"));
        m_pieceFont.setStyleHint(QFont::Serif);
        m_pieceFont.setBold(true);
        m_pieceFont.setPixelSize(int(g.cell * 0.52));
        m_pieceFontCell = cellPx;
    }

    // 单枚棋子完整绘制（阴影+渐变棋身+字）：静态棋子与滑动动画共用
    auto drawOnePiece = [&](const QPointF &center, int code) {
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
        p.setFont(m_pieceFont);
        p.setPen(isRed(code) ? kRedText : kBlackText);
        const QRectF tb(center.x() - radius, center.y() - radius,
                        2 * radius, 2 * radius);
        p.drawText(tb, Qt::AlignCenter, pieceText(code));
    };

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            const int code = coreBoard[r][c];
            if (code == 0)
                continue;
            // 落子动画期间：终点棋子不画静态（由下方滑动棋子顶替）
            if (m_moveAnimActive && r == m_moveAnim.x2 && c == m_moveAnim.y2)
                continue;
            drawOnePiece(dp(r, c), code);
        }
    }

    // ---- 8.5 落子滑动动画：棋子从起点 ease-out 滑向终点（画在最上层）----
    if (m_moveAnimActive && m_moveAnimPiece != 0) {
        const qreal t = qreal(m_moveAnimFrame + 1) / kMoveAnimFrames;
        const qreal e = 1.0 - (1.0 - t) * (1.0 - t);   // ease-out：先快后慢
        const qreal row = m_moveAnim.x1 + (m_moveAnim.x2 - m_moveAnim.x1) * e;
        const qreal col = m_moveAnim.y1 + (m_moveAnim.y2 - m_moveAnim.y1) * e;
        // 浮点版 displayPt（执黑时旋转 180°）
        qreal dr = row, dc = col;
        if (!m_playerRed) { dr = (kRows - 1) - dr; dc = (kCols - 1) - dc; }
        drawOnePiece(QPointF(g.ox + dc * g.cell, g.oy + dr * g.cell),
                     m_moveAnimPiece);
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

// ============================================================================
// 联机模式：收到对方走子 → 在本机棋盘上落子
// ============================================================================
// 中途加入观战：按服务器着法历史从初始布局快放到当前局面（静音、不判胜负、不做长将检测）
void MainWindow::replayMoves(const QJsonArray &moves)
{
    if (!m_isSpectator || moves.isEmpty())
        return;
    m_moveAnimActive = false;   // 快放回放不做动画，先清掉可能残留的滑动状态
    m_moveAnimTimer->stop();
    for (const auto &v : moves) {
        const QJsonObject o = v.toObject();
        const int x1 = o.value(QStringLiteral("x1")).toInt();
        const int y1 = o.value(QStringLiteral("y1")).toInt();
        const int x2 = o.value(QStringLiteral("x2")).toInt();
        const int y2 = o.value(QStringLiteral("y2")).toInt();
        if (x1 < 0 || x1 > 9 || y1 < 0 || y1 > 8 || x2 < 0 || x2 > 9 || y2 < 0 || y2 > 8)
            continue;
        Step step;
        step.captured = coreBoard[x2][y2];
        step.piece = coreBoard[x1][y1];
        step.move = Move(x1, y1, x2, y2);
        step.byRed = isRed(step.piece);
        executeMove(step.move);
        m_history.push_back(step);
        m_last = step.move;
        m_hasLast = true;
    }
    if (m_view)
        m_view->update();
}

// 落子滑动动画：走子落定后调用，棋子从起点 ease-out 滑向终点（看清每步从哪走到哪）
void MainWindow::animateMove(const Move &mv)
{
    if (!m_view || m_moveAnimTimer == nullptr)
        return;
    // 走子已落定：终点格就是刚走的棋子（被吃的子已被移除）
    const int piece = coreBoard[mv.x2][mv.y2];
    if (piece == 0)
        return;
    m_moveAnim = mv;
    m_moveAnimPiece = piece;
    m_moveAnimFrame = 0;
    m_moveAnimActive = true;
    m_moveAnimTimer->start();
}

void MainWindow::onNetworkMoveReceived(int x1, int y1, int x2, int y2)
{
    if (m_mode != GameMode::Online)
        return;
    // 防御：坐标必须在棋盘内，否则 coreBoard 负/越界下标是未定义行为
    if (x1 < 0 || x1 > 9 || y1 < 0 || y1 > 8 || x2 < 0 || x2 > 9 || y2 < 0 || y2 > 8)
        return;

    // 双保险：对局者只可能收到对方颜色的走子（服务器已做权威回合校验，
    // 这里再拦一次本地/回声类的乱序走子，防止棋盘被非法步污染）
    if (!m_isSpectator) {
        const int srcPiece = coreBoard[x1][y1];
        if (srcPiece != 0 && isRed(srcPiece) == m_playerRed)
            return;
    }

    const Move opp(x1, y1, x2, y2);
    Step oppStep;
    oppStep.captured = coreBoard[x2][y2];
    oppStep.piece = coreBoard[x1][y1];
    executeMove(opp);        // 对方已在自己棋盘上校验过，直接落子
    oppStep.move = opp;
    // 对局者：对方走的一定是我方对面的颜色；观战者：按棋子颜色判断（双方走子都会到达）
    oppStep.byRed = m_isSpectator ? isRed(oppStep.piece) : !m_playerRed;
    m_history.push_back(oppStep);
    m_last = opp;
    m_hasLast = true;
    animateMove(opp);   // 落子动画：看清对方/观战双方这步从哪走到哪

    // 对方吃子用对面音效，普通走动用木质落子声
    (oppStep.captured != 0 ? m_sndCapture : m_sndMove)->play();

    // 长将/重复局面检测：对方走子后导致重复局面 → 对方判负（我方赢）
    // 观战者不做判罚（视角无"对方"概念，由双方上报终局），只看棋
    if (!m_isSpectator) {
        const uint64_t newHash = getBoardHash(coreBoard, !m_playerRed);
        // 至少 6 步（3 个完整回合）才可能产生真正的重复局面，少于 6 步不判犯规
        if (m_history.size() >= 6 && isGameRepetition(newHash)) {
            m_gameOver = true;
            statusBar()->showMessage(QStringLiteral("对方长将犯规 —— 你赢了！"));
            pushGameHash(newHash);
            m_view->update();
            if (m_botPlay) {
                botRestartLoop();
                refreshButtons();
                return;
            }
            m_sndWin->play();
            QMessageBox::information(this,
                QStringLiteral("对局结束"),
                QStringLiteral("对方长将犯规，你赢了！\n（连续重复局面自动判负）"));
            reportWinAchievements(QString());
            refreshButtons();
            return;
        }
        pushGameHash(newHash);
    }

    // 判终局（观战者不判：等双方上报 game_end 再收摊）
    const int myState = m_isSpectator ? 0 : checkGameOver(coreBoard, m_playerRed);
    if (myState != 0)
        m_gameOver = true;
    updateCheckState();
    m_myTurn = true;   // 对方走完，轮到我
    m_view->update();

    if (myState != 0) {
        // 互联网：上报终局（对方颜色获胜）
        if (m_net && !m_resultReported) {
            m_resultReported = true;
            m_net->sendGameEnd(m_playerRed
                ? QStringLiteral("black_win") : QStringLiteral("red_win"));
        }
        const QString kill = QString::fromLocal8Bit(
            getKillTypeName(coreBoard, m_playerRed).c_str());
        const QString reason = (myState == 1)
            ? QStringLiteral("你被将死")
            : QStringLiteral("你无子可动（困毙）");
        statusBar()->showMessage(QStringLiteral("对方胜利！") + reason);
        m_animTimer->stop();
        if (m_botPlay) {
            botRestartLoop();
        } else {
            m_sndLose->play();
            QMessageBox::information(this,
                QStringLiteral("对局结束"),
                QStringLiteral("对方胜利！\n%1\n杀法：%2").arg(reason, kill));
        }
    } else if (m_isSpectator) {
        statusBar()->showMessage(QStringLiteral("观战中…"));
    } else {
        statusBar()->showMessage(m_playerRed
            ? QStringLiteral("红方走棋（轮到你了）")
            : QStringLiteral("黑方走棋（轮到你了）"));
        // 挂机中：稍等半秒（留个"停止挂机"的窗口），奶娃自动出手
        if (m_afk) {
            m_afkTimer->start(500);   // 成员 timer：start() 重启而非堆叠，确保只有一个待触发
        }
    }
    refreshButtons();
}

// ============================================================================
// 联机模式：收到对方聊天
// ============================================================================
void MainWindow::onNetworkChatReceived(const QString &text)
{
    if (!m_chatView)
        return;
    QString body = text;
    maybePlayVoice(body, false);   // 对局女号 bot 说话带语音（哼/可恶/嘻嘻…），对手直接对我说话不过滤
    if (body.trimmed().isEmpty())
        return;   // 纯语音消息不留空气泡
    StickerPicker::appendMessage(
        m_chatView, QStringLiteral("<b style='color:#c0392b'>对方：</b>"), body);
}

// ============================================================================
// 联机：奶娃状态同步（房主端广播 → 加入者底部“奶娃帮手”条呈现）
// ============================================================================
QString MainWindow::stateKey(AvatarState s) const
{
    switch (s) {
    case AvatarState::Idle:       return QStringLiteral("idle");
    case AvatarState::Selected:   return QStringLiteral("selected");
    case AvatarState::Thinking:   return QStringLiteral("think");
    case AvatarState::Angry:      return QStringLiteral("angry");
    case AvatarState::Action:     return QStringLiteral("action");
    case AvatarState::GoodMove:   return QStringLiteral("good");
    case AvatarState::UndoAsk:    return QStringLiteral("undo");
    case AvatarState::Listening:  return QStringLiteral("listen");
    case AvatarState::Lose:       return QStringLiteral("lose");
    case AvatarState::Win:        return QStringLiteral("win");
    }
    return QStringLiteral("idle");
}

// 房主：把奶娃状态发给对面（仅联机房主且奶娃已召唤时生效）
void MainWindow::syncAvatar(const QString &key)
{
    if (m_mode == GameMode::Online && m_isHost && m_dragonSummoned && m_net)
        m_net->sendAvatar(key);
}

// 加入者：按收到的关键字渲染底部帮手条（与房主端形象状态一一对应）
void MainWindow::setPeerAvatar(const QString &key)
{
    if (!m_peerBar)
        return;
    if (m_peerMovie->state() == QMovie::Running)
        m_peerMovie->stop();
    if (m_peerMovie2->state() == QMovie::Running)
        m_peerMovie2->stop();

    // 打人动图状态
    if (key == QStringLiteral("action") && m_peerMovie->isValid()) {
        m_peerImg->setMovie(m_peerMovie);
        m_peerMovie->start();
        m_peerMood->setText(QStringLiteral("看我反击！"));
        return;
    }
    if (key == QStringLiteral("action2") && m_peerMovie2->isValid()) {
        m_peerImg->setPixmap(QPixmap());   // 清静态图，交给 frameChanged 回调上屏
        m_peerMovie2->start();
        m_peerMood->setText(QStringLiteral("奶娃出拳！砰！"));
        return;
    }

    QString file = QStringLiteral("idle.jpg");
    QString mood = QStringLiteral("我只是一只小奶龙");
    if (key == QStringLiteral("ready")) {
        mood = QStringLiteral("奶娃已就位！要我替你下棋不？");
    } else if (key == QStringLiteral("selected")) {
        file = QStringLiteral("selected.jpg");  mood = QStringLiteral("绷不住了");
    } else if (key == QStringLiteral("think")) {
        file = QStringLiteral("thinking.jpg");  mood = QStringLiteral("思考中…");
    } else if (key == QStringLiteral("angry")) {
        file = QStringLiteral("angry.jpg");     mood = QStringLiteral("敢吃我的子？！");
    } else if (key == QStringLiteral("good")) {
        file = QStringLiteral("good.jpg");      mood = QStringLiteral("这一步不错！");
    } else if (key == QStringLiteral("undo")) {
        file = QStringLiteral("undo.jpg");      mood = QStringLiteral("什么？你要悔棋？！");
    } else if (key == QStringLiteral("listen")) {
        file = QStringLiteral("music.jpg");     mood = QStringLiteral("♪ 沉浸音乐中…");
    } else if (key == QStringLiteral("lose")) {
        file = QStringLiteral("lose.jpg");      mood = QStringLiteral("呜呜…我输了");
    } else if (key == QStringLiteral("win")) {
        file = QStringLiteral("win.jpg");       mood = QStringLiteral("你真是菜坤");
    }
    m_peerImg->setPixmap(QPixmap(QStringLiteral(":/avatar/") + file)
        .scaled(m_peerImg->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_peerMood->setText(mood);
}

// 加入者：收到房主端奶娃状态（首条消息会让帮手条现身）
void MainWindow::onNetworkAvatar(const QString &key)
{
    if (m_mode != GameMode::Online || m_isHost || m_isSpectator)
        return;
    if (m_peerBar)
        m_peerBar->setVisible(true);
    setPeerAvatar(key);
}

// ============================================================================
// 联机模式：对方投降 → 我方获胜
// ============================================================================
void MainWindow::onNetworkResign()
{
    if (m_mode != GameMode::Online || m_gameOver)
        return;
    m_gameOver = true;
    m_resultReported = true;  // 对方投降已由服务器判负，不再上报 game_end
    clearSelection();
    m_animTimer->stop();
    m_avatarTimer->stop();
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    statusBar()->showMessage(QStringLiteral("对方投降了 —— 你胜利了！"));
    setAvatar(AvatarState::Win);   // 我方（含奶娃）赢 → 笑脸
    m_sndWin->play();
    if (m_chatView)
        m_chatView->append(QStringLiteral("<i style='color:#27ae60'>—— 对方投降，你赢了！ ——</i>"));
    if (m_botPlay) {
        botRestartLoop();
        refreshButtons();
        m_view->update();
        return;
    }
    QMessageBox::information(this,
        QStringLiteral("对局结束"),
        QStringLiteral("对方投降了，你赢了！🎉"));
    reportWinAchievements(QString());
    refreshButtons();
    m_view->update();
}

// ============================================================================
// 联机模式：对方掉线
// ============================================================================
void MainWindow::onPeerDisconnected()
{
    m_gameOver = true;
    m_afk = false;
    if (m_afkBtn)
        m_afkBtn->setText(QStringLiteral("🥱 奶娃挂机"));
    if (m_peerBar)
        m_peerBar->setVisible(false);   // 房主端奶娃随连接一起消失
    if (m_chatView)
        m_chatView->append(QStringLiteral("<i style='color:#888'>—— 对方已断开连接 ——</i>"));
    // 观战：对局收摊不自动关窗（自动关窗用户会以为程序崩溃），留窗看终盘，手动返回
    if (m_isSpectator) {
        statusBar()->showMessage(QStringLiteral("对局已结束，可返回主菜单观看其他对局"));
        if (m_chatView)
            m_chatView->append(QStringLiteral("<i style='color:#888'>—— 本局对局已结束 ——</i>"));
        refreshButtons();
        return;
    }
    // 机器人：不弹窗，关窗回菜单重开下一局
    if (m_botPlay) {
        botRestartLoop();
        refreshButtons();
        return;
    }
    QMessageBox::warning(this,
        QStringLiteral("连接断开"),
        QStringLiteral("对方已离开房间，对局结束。"));
    refreshButtons();
}

// ============================================================================
// 联机模式：发送聊天
// ============================================================================
void MainWindow::sendChatMessage()
{
    if (!m_chatInput || !m_net)
        return;
    const QString text = m_chatInput->text().trimmed();
    if (text.isEmpty())
        return;
    m_net->sendChat(text);
    StickerPicker::appendMessage(
        m_chatView, QStringLiteral("<b style='color:#27ae60'>我：</b>"), text);
    m_chatInput->clear();
}

// ============================================================================
// 联机：版本握手 —— 版本不一致时提示双方更新，防止新旧混用走棋不同步
// ============================================================================
void MainWindow::onNetworkVersion(const QString &ver)
{
    if (ver == QStringLiteral("3.3.0"))
        return;
    if (m_chatView)
        m_chatView->append(QStringLiteral(
            "<i style='color:#c0392b'>⚠ 对方版本（%1）与本机（3.3.0）不一致，"
            "请双方都用最新安装包，否则可能出现走棋不同步</i>").arg(ver.toHtmlEscaped()));
}

// ============================================================================
// 联机：收到越界/非法走子（旧版 bug 会发 -1,-1）—— 不执行，聊天区提示
// ============================================================================
void MainWindow::onNetworkBadMove()
{
    if (m_chatView)
        m_chatView->append(QStringLiteral(
            "<i style='color:#c0392b'>⚠ 收到对方发来的非法走子数据，可能是对方版本过旧，请双方都更新到最新版</i>"));
}

// ============================================================================
// 联机悔棋：请求方发 UNDO REQ，对方同意后双方各撤销一手（回两手=对方+自己）
// ============================================================================
void MainWindow::sendUndoRequest()
{
    if (!m_net || m_waitingUndo)
        return;
    m_waitingUndo = true;
    refreshButtons();
    m_net->sendUndoRequest();
    statusBar()->showMessage(QStringLiteral("已发送悔棋请求，等待对方同意…"));
}

void MainWindow::onNetworkUndoRequest()
{
    // 观战者不参与悔棋（服务器转发的事件，忽略）；机器人自动拒绝不弹窗
    if (m_isSpectator)
        return;
    if (m_botPlay || m_gameOver || m_aiThinking) {
        m_net->sendUndoAnswer(false);
        return;
    }
    const auto ret = QMessageBox::question(this,
        QStringLiteral("悔棋"),
        QStringLiteral("对方请求悔棋，是否同意？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    m_net->sendUndoAnswer(ret == QMessageBox::Yes);
    if (ret == QMessageBox::Yes)
        performOnlineUndo(false);
}

void MainWindow::onNetworkUndoAnswer(bool accepted)
{
    m_waitingUndo = false;
    refreshButtons();
    if (accepted) {
        statusBar()->showMessage(QStringLiteral("对方同意了悔棋"));
        performOnlineUndo(true);
    } else {
        statusBar()->showMessage(QStringLiteral("对方拒绝了悔棋"));
    }
}

// 联机悔棋：双方按"请求方颜色"回退——先撤请求方最后一手之后的所有应手，
// 再撤请求方那手；悔棋后轮到请求方走，两边棋盘保持一致
void MainWindow::performOnlineUndo(bool requesterIsMe)
{
    const bool requesterRed = requesterIsMe ? m_playerRed : !m_playerRed;
    // 请求方最后一手之后，对方可能已经应手——先全部撤掉
    while (!m_history.empty() && m_history.back().byRed != requesterRed) {
        revertStep(m_history.back());
        m_history.pop_back();
    }
    // 再撤请求方要悔的那一手
    if (!m_history.empty() && m_history.back().byRed == requesterRed) {
        revertStep(m_history.back());
        m_history.pop_back();
    }

    m_gameOver = false;
    clearSelection();
    m_moveAnimActive = false;   // 联机悔棋棋盘回退，停掉滑动动画
    m_moveAnimTimer->stop();
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

    // 悔棋后轮到请求方走（回了两手）
    m_myTurn = requesterIsMe;
    statusBar()->showMessage(requesterIsMe
        ? QStringLiteral("悔棋成功，轮到你走")
        : QStringLiteral("对方悔棋成功，轮到对方走"));
}

// ============================================================================
// 联机重新开始：请求方发 AGAIN REQ，对方同意后双方都重开（房主=红方先手）
// ============================================================================
void MainWindow::sendRestartRequest()
{
    if (!m_net || m_waitingRestart)
        return;
    m_waitingRestart = true;
    refreshButtons();
    m_net->sendRestartRequest();
    statusBar()->showMessage(QStringLiteral("已发送重新开始请求，等待对方同意…"));
}

void MainWindow::onNetworkRestartRequest()
{
    // 观战者不参与重开协商；机器人自动拒绝不弹窗
    if (m_isSpectator)
        return;
    if (m_botPlay) {
        m_net->sendRestartAnswer(false);
        return;
    }
    const auto ret = QMessageBox::question(this,
        QStringLiteral("重新开始"),
        QStringLiteral("对方请求重新开始，是否同意？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    m_net->sendRestartAnswer(ret == QMessageBox::Yes);
    if (ret == QMessageBox::Yes)
        applyOnlineRestart();
}

void MainWindow::onNetworkRestartAnswer(bool accepted)
{
    m_waitingRestart = false;
    refreshButtons();
    if (accepted) {
        statusBar()->showMessage(QStringLiteral("对方同意了重新开始"));
        applyOnlineRestart();
    } else {
        statusBar()->showMessage(QStringLiteral("对方拒绝了重新开始"));
    }
}

// 联机重开：两边都调用，保证棋盘完全同步重置
void MainWindow::applyOnlineRestart()
{
    if (m_endgameIdx >= 0)
        initEndgame(m_endgameIdx);
    else
        initBoard();
    resetGameHash();
    resetKillerAndHistory();
    m_history.clear();
    clearSelection();
    m_hasLast = false;
    m_moveAnimActive = false;   // 联机重开，清掉滑动动画
    m_moveAnimTimer->stop();
    m_gameOver = false;
    m_resultReported = false;
    m_aiThinking = false;
    m_animPhase = 0.0;
    m_wasRedInCheck = false;
    m_wasBlackInCheck = false;
    m_avatarTimer->stop();
    setAvatar(AvatarState::Idle);
    // 联机：房主=红方先手
    m_myTurn = m_isHost;
    updateCheckState();
    refreshButtons();
    m_view->update();
    statusBar()->showMessage(m_isHost
        ? QStringLiteral("新对局开始：你执红先行")
        : QStringLiteral("新对局开始：对方执红先行"));
}

// ============================================================================
// 联机房主专属：召唤奶龙 → 播放登场动画（跳舞+音乐），之后解锁"奶娃挂机"
// ============================================================================
void MainWindow::summonDragon()
{
    if (m_mode != GameMode::Online || !m_isHost || m_gameOver
        || !m_myTurn || m_dragonSummoned)
        return;

    // 登场视频自带音乐：先暂停背景音乐，播完再恢复
    ensureMusicPlayer();
    const bool musicWasPlaying =
        m_musicPlayer->playbackState() == QMediaPlayer::PlayingState;
    if (musicWasPlaying)
        m_musicPlayer->pause();

    DragonVideoDialog dlg(this);
    dlg.exec();

    if (musicWasPlaying)
        m_musicPlayer->play();

    // 召唤完成：形象面板现身，召唤钮退场，挂机钮登场
    m_dragonSummoned = true;
    m_avatarPanel->setVisible(true);
    m_summonBtn->setVisible(false);
    m_afkBtn->setVisible(true);
    m_avatarMood->setText(QStringLiteral("奶娃已就位！要我替你下棋不？"));
    syncAvatar(QStringLiteral("ready"));   // 对面屏幕底部出现奶娃帮手条
    if (m_chatView)
        m_chatView->append(QStringLiteral("<i style='color:#8e44ad'>🐲 奶龙登场！可开启“奶娃挂机”让它替你出战</i>"));
    statusBar()->showMessage(QStringLiteral("奶龙已登场，点击“奶娃挂机”开始自动对战"));
}

// ============================================================================
// 奶娃挂机：开启后轮到房主时自动想棋、自动落子并把走法发给对方
// ============================================================================
void MainWindow::toggleAfk()
{
    if (!m_dragonSummoned || m_gameOver)
        return;

    if (!m_afk) {
        m_afk = true;
        m_afkBtn->setText(QStringLiteral("⏹ 停止挂机"));
        statusBar()->showMessage(QStringLiteral("奶娃开始挂机代打…"));
        if (m_chatView)
            m_chatView->append(QStringLiteral("<i style='color:#8e44ad'>🐲 奶娃：交给我，你去歇着！</i>"));
        startAfkMove();   // 若当前轮到房主，立即出手
    } else {
        m_afk = false;
        m_afkBtn->setText(QStringLiteral("🥱 奶娃挂机"));
        statusBar()->showMessage(QStringLiteral("已停止挂机，接下来你自己走"));
        // 正在思考的那一步让它走完（走完不再自动续），思考脸提前收回
        if (!m_aiThinking && !m_avatarTimer->isActive())
            setAvatar(AvatarState::Idle);
    }
}

// 挂机：奶娃替房主启动后台搜索（执房主方颜色，固定思考 3 秒）
void MainWindow::startAfkMove()
{
    if (!m_afk || m_gameOver || !m_myTurn || m_aiThinking)
        return;

    m_aiThinking = true;
    refreshButtons();
    if (!m_avatarTimer->isActive())
        setAvatar(AvatarState::Thinking);

    m_aiThread = new AiThread();
    std::memcpy(m_aiThread->m_board, coreBoard, sizeof(coreBoard));
    // 3 秒思考：搜索在独立 QThread 中进行，不阻塞 UI；
    // 实时对局不追求最强棋力，短思考响应更流畅
    m_aiThread->m_timeMs = 3000;
    m_aiThread->m_redSide = m_playerRed;   // 替房主（我方）想
    connect(m_aiThread, &QThread::finished, this, &MainWindow::onAfkFinished);
    connect(m_aiThread, &QThread::finished, m_aiThread, &QObject::deleteLater);

    m_thinkClock->restart();
    m_thinkTimer->start();
    onAiTick();
    m_aiThread->start();
}

// 挂机搜索结束：奶娃自动落子 → 随机吃子效果 → 判终局 → 发给对方
void MainWindow::onAfkFinished()
{
    const Move mv = m_aiThread->m_result;
    m_aiThread = nullptr;
    m_thinkTimer->stop();
    m_aiThinking = false;
    m_myTurn = false;   // 提前标记不走子：防止后续代码触发的事件中 startAfkMove 误判轮到自己

    // 兜底：我方无棋可走 = 我方输
    if (!(inBoard(mv.x1, mv.y1) && inBoard(mv.x2, mv.y2))) {
        m_gameOver = true;
        m_animTimer->stop();
        statusBar()->showMessage(QStringLiteral("你方无棋可走，对方胜利"));
        setAvatar(AvatarState::Win);
        m_sndLose->play();
        m_view->update();
        if (m_botPlay) {
            botRestartLoop();
            refreshButtons();
            return;
        }
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("对方胜利！\n你方无子可动（困毙）"));
        refreshButtons();
        return;
    }

    // ---- 奶娃落子，记入历史 ----
    Step s;
    s.captured = coreBoard[mv.x2][mv.y2];
    s.piece = coreBoard[mv.x1][mv.y1];
    executeMove(mv);
    s.move = mv;
    s.byRed = m_playerRed;
    m_history.push_back(s);
    m_last = mv;
    m_hasLast = true;
    animateMove(mv);   // 落子动画：奶娃挂机这步滑动

    // ---- 奶娃落子后：长将/重复局面检测 ----
    {
        const uint64_t newHash = getBoardHash(coreBoard, m_playerRed);
        if (m_history.size() >= 6 && isGameRepetition(newHash)) {
            m_gameOver = true;
            statusBar()->showMessage(QStringLiteral("奶娃长将犯规 —— 你输了！"));
            m_sndLose->play();
            pushGameHash(newHash);
            m_view->update();
            if (m_botPlay) {
                botRestartLoop();
                refreshButtons();
                return;
            }
            QMessageBox::information(this,
                QStringLiteral("对局结束"),
                QStringLiteral("奶娃长将犯规，你输了！\n（连续重复局面自动判负）"));
            refreshButtons();
            if (m_mode == GameMode::SinglePlayer) {
                m_levelCombo->setEnabled(true);
                m_sideCombo->setEnabled(true);
            }
            return;
        }
        pushGameHash(newHash);
    }

    // ---- 奶娃吃子：音效和打人动画各自随机二选一 ----
    if (s.captured != 0) {
        const bool newSfx = QRandomGenerator::global()->bounded(2) == 0;
        (newSfx ? m_sndCapture2 : m_sndCapture)->play();
        playAfkCaptureAction(QRandomGenerator::global()->bounded(2) == 0);
    } else {
        // 非吃子：奶龙保持平时动作，收回思考脸
        m_sndMove->play();
        setAvatar(AvatarState::Idle);
    }
    clearSelection();

    // ---- 判对面终局 ----
    const int oppState = checkGameOver(coreBoard, !m_playerRed);
    if (oppState != 0)
        m_gameOver = true;
    updateCheckState();
    m_view->update();

    // ---- 走法必须在弹任何终局对话框之前发出：模态对话框会阻塞事件循环，
    // 若先弹窗，对方要等到本方点掉弹窗才能收到这步（看着就像"卡死在将军一步"）----
    if (m_net)
        m_net->sendMove(mv.x1, mv.y1, mv.x2, mv.y2);

    if (oppState != 0) {
        const QString kill = QString::fromLocal8Bit(
            getKillTypeName(coreBoard, !m_playerRed).c_str());
        const QString reason = (oppState == 1)
            ? QStringLiteral("对面被将死")
            : QStringLiteral("对面无子可动（困毙）");
        statusBar()->showMessage(QStringLiteral("奶娃帮你赢了！") + reason);
        m_animTimer->stop();
        m_avatarTimer->stop();
        setAvatar(AvatarState::Lose);
        m_sndWin->play();
        m_view->update();
        if (m_chatView)
            m_chatView->append(QStringLiteral("<i style='color:#8e44ad'>🐲 奶娃：轻轻松松，带你吃鸡！</i>"));
        if (m_botPlay) {
            botRestartLoop();
            refreshButtons();
            return;
        }
        QMessageBox::information(this,
            QStringLiteral("对局结束"),
            QStringLiteral("🎉 奶娃代打胜利！\n%1\n杀法：%2").arg(reason, kill));
        reportWinAchievements(kill);
    } else {
        // 对方走后 onNetworkMoveReceived 会再触发奶娃
        statusBar()->showMessage(QStringLiteral("奶娃已走子，等待对方…"));
    }
    refreshButtons();
}

// 奶娃吃子后的打人动作：新版出拳 gif / 原版打人 gif 随机
void MainWindow::playAfkCaptureAction(bool useNewGif)
{
    m_avatarTimer->stop();
    m_avatarState = AvatarState::Action;
    syncAvatar(useNewGif ? QStringLiteral("action2") : QStringLiteral("action"));

    if (useNewGif && m_avatarMovie2->isValid()) {
        if (m_avatarMovie->state() == QMovie::Running)
            m_avatarMovie->stop();
        m_avatarMood->setText(QStringLiteral("奶娃出拳！砰！"));
        m_avatarMovie2->start();   // frameChanged 回调负责缩放上屏
        m_avatarTimer->start(2400);
    } else {
        if (m_avatarMovie2->state() == QMovie::Running)
            m_avatarMovie2->stop();
        m_avatarMood->setText(QStringLiteral("看我反击！"));
        m_avatarImg->setMovie(m_avatarMovie);
        m_avatarMovie->start();
        m_avatarTimer->start(1300);
    }
}

// ============================================================================
// 观战相关：观众发言显示 / 终局结果展示
// ============================================================================
// 女号语音：检测 [voice:xxx] 前缀，摘前缀+播放对应音效（机器人/观战静音时不播）
QString MainWindow::s_localName;
int MainWindow::s_openGameWindows = 0;

void MainWindow::maybePlayVoice(QString &text, bool ambient)
{
    static const QRegularExpression re(QStringLiteral("^\\[voice:([a-z0-9]+)\\]"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return;
    const QString key = m.captured(1);
    text = text.mid(m.capturedLength());
    if (m_botPlay)
        return;   // 机器人后台静音
    // 环境闲聊（观战席/对局里的观众发言）只听提到自己昵称的语音，其余静默
    if (ambient && !s_localName.isEmpty() && !text.contains(s_localName))
        return;
    auto *&fx = m_voiceFx[key];
    if (!fx) {
        fx = new QSoundEffect(this);
        fx->setSource(QUrl(QStringLiteral("qrc:/sounds/voice/%1.wav").arg(key)));
        fx->setVolume(0.9f);
    }
    if (fx->status() == QSoundEffect::Ready || !fx->source().isEmpty())
        fx->play();
}

void MainWindow::onSpecChatReceived(const QString &fromName, const QString &fromAvatar,
                                    const QString &text)
{
    if (!m_chatView)
        return;
    QString body = text;
    maybePlayVoice(body, true);   // 观战席=环境闲聊，只有 @我 才出声
    if (body.trimmed().isEmpty())
        return;   // 纯语音消息不留空气泡
    // 头像：磁盘缓存有就直接 <img> 圆头像，没有就只显名字（后台触发下载，下条消息起有头像）
    QString avatarImg;
    if (!fromAvatar.isEmpty()) {
        const QString fullUrl = ServerConfig::serverUrl() + fromAvatar;
        const QString cached = ApiClient::avatarCachePath(fullUrl);
        if (!cached.isEmpty()) {
            avatarImg = QStringLiteral("<img src='file:///%1' width='20' height='20'> ")
                            .arg(QString(cached).replace('\\', '/'));
        } else {
            // 懒下载进缓存（共享一个静态 ApiClient，MainWindow 本体不持有）
            static QPointer<ApiClient> s_dl = nullptr;
            if (!s_dl) s_dl = new ApiClient(qApp);
            s_dl->downloadAvatar(fullUrl);
        }
    }
    const QString safe = fromName.toHtmlEscaped();
    const QString prefix = m_isSpectator
        ? QStringLiteral("%1<b style='color:#8e44ad'>%2：</b>").arg(avatarImg, safe)
        : QStringLiteral("%1<b style='color:#8e44ad'>观众 %2：</b>").arg(avatarImg, safe);
    StickerPicker::appendMessage(m_chatView, prefix, body);
}

void MainWindow::onGameEndReceived(const QString &result)
{
    if (m_mode != GameMode::Online)
        return;
    m_gameOver = true;
    m_resultReported = true;
    const QString txt = result == QLatin1String("red_win") ? QStringLiteral("红方胜利")
                       : result == QLatin1String("black_win") ? QStringLiteral("黑方胜利")
                       : QStringLiteral("平局");
    statusBar()->showMessage(QStringLiteral("对局结束：%1").arg(txt));
    if (m_chatView)
        m_chatView->append(QStringLiteral(
            "<i style='color:#27ae60'>—— 对局结束：%1 ——</i>").arg(txt));
    refreshButtons();
    m_view->update();
    if (m_botPlay)
        botRestartLoop();
}

// ============================================================================
// 机器人代打（--bot 命令行模式）
// ============================================================================
// 女号对局机器人语音标记与表情混发（可爱/傲娇/冰冷三系各自语料在 keepalive 中，
// 对局 bot 这里的语料走通用撒娇语气，30% 概率带语音前缀）
QString MainWindow::botChatLine()
{
    static const QStringList lines = {
        QStringLiteral("[sticker:s01]"),
        QStringLiteral("[sticker:s03]"),
        QStringLiteral("[sticker:s05]"),
        QStringLiteral("[sticker:s07]"),
        QStringLiteral("[sticker:s09]"),
        QStringLiteral("[sticker:s11]"),
        // 真人甜妹录音为主（v1~v6），合成语音只留少量点缀
        QStringLiteral("[voice:v1]"),
        QStringLiteral("[voice:v2]"),
        QStringLiteral("[voice:v3]"),
        QStringLiteral("[voice:v4]"),
        QStringLiteral("[voice:v5]"),
        QStringLiteral("[voice:v6]"),
        QStringLiteral("[voice:giggle]嘻嘻～加油加油！"),
        QStringLiteral("[voice:lihai]厉害呀！"),
        QStringLiteral("[voice:heng]下一把换我先走"),
        // 闲聊短句：像真人一样边下边聊
        QStringLiteral("你走得好快呀，等等我嘛"),
        QStringLiteral("这局我要认真了哦"),
        QStringLiteral("哎呀，差点走错了"),
        QStringLiteral("你的棋下得还不错嘛"),
        QStringLiteral("我就随便走走啦，别笑话我"),
        QStringLiteral("嘿嘿，看我这一步！"),
        QStringLiteral("别催别催，人家在想呢"),
        QStringLiteral("这棋走得我手心冒汗"),
        QStringLiteral("你先说话，我一紧张就走不好了"),
        QStringLiteral("输了可不许笑我哦"),
    };
    return lines.at(QRandomGenerator::global()->bounded(lines.size()));
}

// 机器人：跳过召唤动画直接开启奶娃挂机代打（红黑双方都可用）
void MainWindow::enableBotAfk()
{
    if (m_mode != GameMode::Online || m_isSpectator || m_gameOver || m_botChatTimer)
        return;
    m_dragonSummoned = true;
    m_afk = true;
    if (m_afkBtn) {
        m_afkBtn->setVisible(true);
        m_afkBtn->setText(QStringLiteral("⏹ 停止挂机"));
    }
    if (m_summonBtn)
        m_summonBtn->setVisible(false);
    // 机器人闲聊：随机 20~90 秒发一句表情/短语（对方与观战者都可见）
    m_botChatTimer = new QTimer(this);
    m_botChatTimer->setSingleShot(true);
    connect(m_botChatTimer, &QTimer::timeout, this, [this]() {
        if (m_gameOver || !m_net)
            return;
        m_net->sendChat(botChatLine());
        m_botChatTimer->start(QRandomGenerator::global()->bounded(20000, 90000));
    });
    m_botChatTimer->start(QRandomGenerator::global()->bounded(15000, 40000));
    startAfkMove();   // 若当前轮到本方，立即出手；否则等对方走完自动续
}

// 机器人：终局后稍等片刻直接退出进程（由编排脚本重启进下一局）
void MainWindow::botRestartLoop()
{
    if (!m_botPlay)
        return;
    // 留 1.5 秒给服务器转发终局/对方收摊，然后绕开 closeEvent 直接退出，
    // 避免模态弹窗/事件循环阻塞导致进程挂死
    QTimer::singleShot(1500, this, [this]() {
        if (m_net) {
            if (m_isSpectator)
                m_net->sendSpectateLeave();
            else
                m_net->leaveRoom();
        }
        qApp->exit(0);
    });
}

