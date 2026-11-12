#include "StartScreen.h"
#include "MainWindow.h"
#include "NetworkManager.h"
#include "LoginDialog.h"
#include "FriendsPanel.h"
#include "InternetClient.h"
#include "ProfileDialog.h"
#include "ShopDialog.h"
#include "MailDialog.h"
#include "ChatDialog.h"
#include "BagDialog.h"
#include "AchievementsDialog.h"
#include "Inventory.h"
#include "ServerConfig.h"
#include "api.h"

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QStackedWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QInputDialog>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSettings>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>

// 房间号 1000~9999 映射到 TCP 端口 31000~39999
quint16 StartScreen::roomPort(const QString &code)
{
    return static_cast<quint16>(30000 + code.toUInt());
}

// 唯一存活的主菜单实例：对局结束新建主菜单时替换旧（隐藏）实例，
// 旧实例持有的互联网 WebSocket 也随之切换到新实例（短暂离线后自动重连）
static StartScreen *s_liveMenu = nullptr;

StartScreen::StartScreen(QWidget *parent)
    : QWidget(parent)
{
    // 销毁上一个被对局窗遮挡的主菜单（它的 WS 在本局刚结束、房间已离开后不再需要）
    if (s_liveMenu && s_liveMenu != this) {
        StartScreen *old = s_liveMenu;
        s_liveMenu = nullptr;
        old->m_wsWanted = false;
        delete old;
    }
    s_liveMenu = this;

    setWindowTitle(QStringLiteral("糖果象棋"));
    resize(480, 660);

    m_wsRetry = new QTimer(this);
    m_wsRetry->setSingleShot(true);
    connect(m_wsRetry, &QTimer::timeout, this, [this]() {
        if (m_wsWanted && m_internet && !m_internet->isOpen())
            m_internet->open(m_accessToken);
    });

    // REST 客户端（互联网模式用，QObject 父子关系自动释放）
    m_api = new ApiClient(this);

    m_stack = new QStackedWidget(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(52, 26, 52, 22);
    root->addWidget(m_stack);

    // ===== 第0页：主菜单 =====
    auto *menu = new QWidget();
    auto *menuLayout = new QVBoxLayout(menu);
    auto *title = new QLabel(QStringLiteral("糖果象棋3.2"), menu);
    title->setAlignment(Qt::AlignCenter);
    QFont f = title->font();
    f.setPointSize(28);
    f.setBold(true);
    title->setFont(f);
    menuLayout->addWidget(title);
    menuLayout->addSpacing(10);

    // ---- 用户条（登录后显示） ----
    m_userBar = new QWidget(menu);
    auto *userBarLayout = new QHBoxLayout(m_userBar);
    userBarLayout->setContentsMargins(0, 0, 0, 0);

    m_userAvatar = new QLabel(m_userBar);
    m_userAvatar->setFixedSize(48, 48);
    m_userAvatar->setStyleSheet(
        "border: 2px solid #ddd; border-radius: 24px; background: #f5f5f5;");
    m_userAvatar->setAlignment(Qt::AlignCenter);
    m_userAvatar->setText(QStringLiteral("头"));
    userBarLayout->addWidget(m_userAvatar);

    auto *userInfoLayout = new QVBoxLayout;
    m_userName = new QLabel(m_userBar);
    m_userName->setStyleSheet(QStringLiteral("font-size:16px;font-weight:bold;"));
    userInfoLayout->addWidget(m_userName);
    m_userTitle = new QLabel(m_userBar);
    m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;"));
    userInfoLayout->addWidget(m_userTitle);
    userBarLayout->addLayout(userInfoLayout);
    userBarLayout->addStretch();

    m_profileBtn = new QPushButton(QStringLiteral("个人资料"), m_userBar);
    m_profileBtn->setFlat(true);
    m_profileBtn->setStyleSheet("color: #1976d2;");
    connect(m_profileBtn, &QPushButton::clicked, this, &StartScreen::onOpenProfile);
    userBarLayout->addWidget(m_profileBtn);

    m_logoutBtn = new QPushButton(QStringLiteral("退出登录"), m_userBar);
    m_logoutBtn->setFlat(true);
    m_logoutBtn->setStyleSheet("color: #c00;");
    connect(m_logoutBtn, &QPushButton::clicked, this, &StartScreen::onLogout);
    userBarLayout->addWidget(m_logoutBtn);

    m_userBar->setVisible(false);  // 登录后才显示
    menuLayout->addWidget(m_userBar);
    menuLayout->addSpacing(16);

    m_singleBtn = new QPushButton(QStringLiteral("开始游戏（人机对战）"), menu);
    m_onlineBtn = new QPushButton(QStringLiteral("联机模式（局域网）"), menu);
    m_internetBtn = new QPushButton(QStringLiteral("互联网对战"), menu);
    m_friendsBtn = new QPushButton(QStringLiteral("好友"), menu);
    auto *endgameBtn = new QPushButton(QStringLiteral("残局挑战"), menu);
    auto *shopBtn = new QPushButton(QStringLiteral("糖果商城"), menu);
    auto *bagBtn = new QPushButton(QStringLiteral("背包"), menu);
    auto *achBtn = new QPushButton(QStringLiteral("成就"), menu);

    for (auto *b : {m_singleBtn, m_onlineBtn, m_internetBtn, m_friendsBtn,
                    endgameBtn, shopBtn, bagBtn, achBtn}) {
        b->setMinimumHeight(40);
        b->setStyleSheet(QStringLiteral("font-size:15px;"));
    }

    menuLayout->setSpacing(8);
    for (auto *b : {m_singleBtn, m_onlineBtn, m_internetBtn, m_friendsBtn,
                    endgameBtn, shopBtn, bagBtn, achBtn}) {
        menuLayout->addWidget(b);
    }
    menuLayout->addStretch();

    // 红点（好友按钮右上）
    m_friendsBadge = new QLabel(m_friendsBtn);
    m_friendsBadge->setFixedSize(18, 18);
    m_friendsBadge->setAlignment(Qt::AlignCenter);
    m_friendsBadge->setStyleSheet(QStringLiteral(
        "background:#fa5252;color:white;border-radius:9px;"
        "font-size:11px;font-weight:bold;"));
    m_friendsBadge->hide();

    // 待回应对战邀请红点（互联网对战按钮右上）
    m_inviteBadge = new QLabel(m_internetBtn);
    m_inviteBadge->setFixedSize(18, 18);
    m_inviteBadge->setAlignment(Qt::AlignCenter);
    m_inviteBadge->setStyleSheet(QStringLiteral(
        "background:#fa5252;color:white;border-radius:9px;"
        "font-size:11px;font-weight:bold;"));
    m_inviteBadge->hide();

    // 角落小按钮：左下邮件+音乐开关，右下制作者说明
    auto *cornerRow = new QHBoxLayout();
    m_mailBtn = new QPushButton(QStringLiteral("邮件"), menu);
    m_mailBtn->setFlat(true);
    m_mailBtn->setStyleSheet(QStringLiteral("font-size:13px;color:#e8890c;"));
    cornerRow->addWidget(m_mailBtn);
    // 邮件红点
    m_mailBadge = new QLabel(m_mailBtn);
    m_mailBadge->setFixedSize(16, 16);
    m_mailBadge->setStyleSheet(QStringLiteral(
        "background:#fa5252;border-radius:8px;"));
    m_mailBadge->hide();
    m_bgmBtn = new QPushButton(menu);
    m_bgmBtn->setFlat(true);
    m_bgmBtn->setStyleSheet(QStringLiteral("font-size:13px;color:#888;"));
    m_bgmOn = QSettings().value(QStringLiteral("startMusicOn"), true).toBool();
    updateBgmBtnText();
    connect(m_bgmBtn, &QPushButton::clicked, this, [this]() {
        m_bgmOn = !m_bgmOn;
        QSettings().setValue(QStringLiteral("startMusicOn"), m_bgmOn);
        updateBgmBtnText();
        if (m_bgmOn)
            startBgm();
        else
            stopBgm();
    });
    cornerRow->addWidget(m_bgmBtn);
    auto *serverBtn = new QPushButton(QStringLiteral("服务器"), menu);
    serverBtn->setFlat(true);
    serverBtn->setStyleSheet(QStringLiteral("font-size:13px;color:#888;"));
    connect(serverBtn, &QPushButton::clicked, this, &StartScreen::onEditServerUrl);
    cornerRow->addWidget(serverBtn);
    cornerRow->addStretch();
    auto *aboutBtn = new QPushButton(QStringLiteral("制作者说明"), menu);
    aboutBtn->setFlat(true);
    aboutBtn->setStyleSheet(QStringLiteral("font-size:13px;color:#888;"));
    connect(aboutBtn, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, QStringLiteral("制作者说明"),
                                 QStringLiteral("因为作者是个穷比没钱租云服务器，所以只能搞内网穿透.翻译成大白话就是只有我电脑开着作为隧道连接后端你们互联网联机功能才能用，其他情况用用局域网得了.局域网要求两个电脑连同一个无线网，最好不要是校园网，用手机开热点，把梯子也关掉"));
    });
    cornerRow->addWidget(aboutBtn);
    menuLayout->addLayout(cornerRow);
    m_stack->addWidget(menu);

    // ===== 第1页：联机选择 =====
    auto *online = new QWidget();
    auto *onlineLayout = new QVBoxLayout(online);
    m_createBtn = new QPushButton(QStringLiteral("创建房间"), online);
    m_joinBtn = new QPushButton(QStringLiteral("加入房间"), online);
    m_backBtn = new QPushButton(QStringLiteral("返回"), online);
    for (auto *b : {m_createBtn, m_joinBtn}) {
        b->setMinimumHeight(56);
        b->setStyleSheet(QStringLiteral("font-size:18px;"));
    }
    onlineLayout->addStretch();
    onlineLayout->addWidget(m_createBtn);
    onlineLayout->addWidget(m_joinBtn);
    onlineLayout->addWidget(m_backBtn);
    onlineLayout->addStretch();
    m_stack->addWidget(online);

    // ===== 第2页：等待对方连接（房主看大号房间号） =====
    auto *wait = new QWidget();
    auto *waitLayout = new QVBoxLayout(wait);
    auto *waitTitle = new QLabel(QStringLiteral("等待对方加入…"), wait);
    waitTitle->setAlignment(Qt::AlignCenter);
    waitTitle->setStyleSheet(QStringLiteral("font-size:22px;font-weight:bold;"));
    m_statusLabel = new QLabel(wait);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size:16px;color:#555;"));
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), wait);
    waitLayout->addStretch();
    waitLayout->addWidget(waitTitle);
    waitLayout->addSpacing(16);
    waitLayout->addWidget(m_statusLabel);
    waitLayout->addSpacing(32);
    waitLayout->addWidget(cancelBtn);
    waitLayout->addStretch();
    m_stack->addWidget(wait);

    // ===== 第3页：加入房间（自动扫描 + 输入房间号） =====
    auto *join = new QWidget();
    auto *joinLayout = new QVBoxLayout(join);
    auto *joinTitle = new QLabel(QStringLiteral("局域网内的房间"), join);
    joinTitle->setAlignment(Qt::AlignCenter);
    joinTitle->setStyleSheet(QStringLiteral("font-size:18px;font-weight:bold;"));
    m_roomList = new QListWidget(join);
    m_roomList->setStyleSheet(QStringLiteral("font-size:16px;"));
    m_codeInput = new QLineEdit(join);
    m_codeInput->setPlaceholderText(QStringLiteral("也可以手动输入4位房间号"));
    m_codeInput->setMaxLength(4);
    m_codeInput->setMinimumHeight(40);
    m_codeInput->setStyleSheet(QStringLiteral("font-size:18px;"));
    auto *connectBtn = new QPushButton(QStringLiteral("加入房间"), join);
    connectBtn->setMinimumHeight(48);
    m_spectateBtn = new QPushButton(QStringLiteral("观战加入"), join);
    m_spectateBtn->setMinimumHeight(48);
    m_spectateBtn->setStyleSheet(QStringLiteral("QPushButton{background:#6c5ce7;color:white;border:none;border-radius:6px;}"));
    auto *backFromJoin = new QPushButton(QStringLiteral("← 返回"), join);
    joinLayout->addWidget(joinTitle);
    joinLayout->addSpacing(8);
    joinLayout->addWidget(m_roomList, 3);
    joinLayout->addSpacing(8);
    joinLayout->addWidget(m_codeInput);
    joinLayout->addSpacing(12);
    joinLayout->addWidget(connectBtn);
    joinLayout->addWidget(m_spectateBtn);
    joinLayout->addWidget(backFromJoin);
    m_stack->addWidget(join);

    // ===== 第4页：正在自动登录… =====
    auto *loading = new QWidget();
    auto *loadingLayout = new QVBoxLayout(loading);
    auto *loadingLbl = new QLabel(QStringLiteral("正在自动登录…"), loading);
    loadingLbl->setAlignment(Qt::AlignCenter);
    loadingLbl->setStyleSheet(QStringLiteral("font-size:18px;color:#666;"));
    loadingLayout->addStretch();
    loadingLayout->addWidget(loadingLbl);
    loadingLayout->addStretch();
    m_stack->addWidget(loading);

    // ===== 信号连接 =====
    connect(m_singleBtn, &QPushButton::clicked, this, &StartScreen::onStartSingle);
    connect(m_onlineBtn, &QPushButton::clicked, this, &StartScreen::onShowOnline);
    connect(m_internetBtn, &QPushButton::clicked, this, &StartScreen::onInternetMode);
    connect(m_friendsBtn, &QPushButton::clicked, this, &StartScreen::onFriends);
    connect(shopBtn, &QPushButton::clicked, this, [this]() {
        ShopDialog dlg(this);
        dlg.exec();
    });
    connect(bagBtn, &QPushButton::clicked, this, [this]() {
        BagDialog dlg(this);
        dlg.exec();
    });
    connect(achBtn, &QPushButton::clicked, this, [this]() {
        AchievementsDialog dlg(m_api, m_accessToken, m_currentUser.title, this);
        connect(&dlg, &AchievementsDialog::titleChanged, this,
                [this](const UserInfo &u) {
            m_currentUser = u;
            m_userName->setText(u.display_name);
            const QString t = u.title.isEmpty() ? QStringLiteral("全服最拉") : u.title;
            m_userTitle->setText(QStringLiteral("◇ %1").arg(t));
            if (t == QStringLiteral("全服第一"))
                m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#d4a017;font-weight:bold;"));
            else if (t == QStringLiteral("全服第二"))
                m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#1976d2;"));
            else
                m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#9c36b5;font-weight:bold;"));
        });
        dlg.exec();
    });
    connect(m_mailBtn, &QPushButton::clicked, this, [this]() {
        MailDialog dlg(this);
        dlg.exec();
        refreshBadges();
    });
    connect(endgameBtn, &QPushButton::clicked, this, &StartScreen::onPickEndgame);
    connect(m_backBtn, &QPushButton::clicked, this, &StartScreen::onBackToMenu);
    connect(m_createBtn, &QPushButton::clicked, this, &StartScreen::onCreateRoom);
    connect(m_joinBtn, &QPushButton::clicked, this, &StartScreen::onJoinPage);
    connect(connectBtn, &QPushButton::clicked, this, &StartScreen::onJoinRoom);
    connect(m_spectateBtn, &QPushButton::clicked, this, &StartScreen::onSpectateJoin);
    connect(m_roomList, &QListWidget::itemDoubleClicked, this, &StartScreen::onJoinRoom);
    connect(backFromJoin, &QPushButton::clicked, this, &StartScreen::onBackToMenu);
    connect(cancelBtn, &QPushButton::clicked, this, &StartScreen::onBackToMenu);

    // 启动时先停在加载页，然后尝试自动登录
    m_stack->setCurrentIndex(4);
    QTimer::singleShot(0, this, &StartScreen::bootstrapAuth);
}

// ===================== 强制登录流程 =====================

void StartScreen::bootstrapAuth()
{
    const QString rt = ApiClient::loadRefreshToken();
    // 没地址时不做自动登录尝试，直接弹登录框让玩家先填服务器地址
    if (rt.isEmpty() || !ServerConfig::isConfigured()) {
        showForcedLogin();
        return;
    }
    m_refreshToken = rt;

    // 用一次性连接做自动登录，成功/失败后断开，防止后续 API 错误误踢
    auto *conn = new QMetaObject::Connection;
    *conn = connect(m_api, &ApiClient::refreshOk, this, [this, conn](const QString &at) {
        m_accessToken = at;
        m_api->getMe(at);
    });
    auto *conn2 = new QMetaObject::Connection;
    *conn2 = connect(m_api, &ApiClient::meOk, this, [this, conn, conn2](const UserInfo &u) {
        m_currentUser = u;
        disconnect(*conn); disconnect(*conn2);
        delete conn; delete conn2;
        enterMenu();
    });
    auto *conn3 = new QMetaObject::Connection;
    *conn3 = connect(m_api, &ApiClient::errorOccurred, this, [this, conn, conn2, conn3](const QString &) {
        ApiClient::clearRefreshToken();
        disconnect(*conn); disconnect(*conn2); disconnect(*conn3);
        delete conn; delete conn2; delete conn3;
        showForcedLogin();
    });

    m_api->refreshAccessToken(rt);
}

void StartScreen::showForcedLogin()
{
    LoginDialog dlg(m_api, this);
    dlg.setForced(true);
    if (dlg.exec() != QDialog::Accepted)
        return;  // 用户在强制模式点关闭 = 退出程序（LoginDialog::reject 已处理）

    m_currentUser = dlg.user();
    m_accessToken = dlg.accessToken();
    m_refreshToken = dlg.refreshToken();
    enterMenu();
}

void StartScreen::enterMenu()
{
    // 填充用户条
    m_userName->setText(m_currentUser.display_name);
    m_userName->setStyleSheet(QStringLiteral("font-size:16px;font-weight:bold;"));

    // 头衔（服务端已按手机号/游客下发；客户端兜底防空）
    QString title = m_currentUser.title;
    if (title.isEmpty())
        title = QStringLiteral("全服最拉");
    m_userTitle->setText(QStringLiteral("◇ %1").arg(title));
    if (title == QStringLiteral("全服第一"))
        m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#d4a017;font-weight:bold;"));
    else if (title == QStringLiteral("全服第二"))
        m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#1976d2;"));
    else
        m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#999;"));

    // 头像
    if (!m_currentUser.avatar_url.isEmpty()) {
        connect(m_api, &ApiClient::avatarDownloaded, this, [this](const QString &, const QPixmap &pm) {
            // 圆形裁剪
            QPixmap scaled = pm.scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = (scaled.width() - 48) / 2, y = (scaled.height() - 48) / 2;
            QPixmap cropped = scaled.copy(x, y, 48, 48);
            QPixmap out(48, 48);
            out.fill(Qt::transparent);
            QPainter p(&out);
            p.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addEllipse(0, 0, 48, 48);
            p.setClipPath(path);
            p.drawPixmap(0, 0, cropped);
            p.end();
            m_userAvatar->setPixmap(out);
        }, Qt::SingleShotConnection);
        m_api->downloadAvatar(ServerConfig::serverUrl() + m_currentUser.avatar_url);
    }

    m_userBar->setVisible(true);
    m_stack->setCurrentIndex(0);
    refreshBadges();

    // 登录态建立后立即常连实时通道：回主菜单、进对局期间都保持在线
    ensureInternet();
}

void StartScreen::onLogout()
{
    if (!m_refreshToken.isEmpty())
        m_api->logout(m_refreshToken);  // 不阻塞等待响应
    ApiClient::clearRefreshToken();
    m_wsWanted = false;
    m_wsRetry->stop();
    if (m_internet)
        m_internet->close();
    m_currentUser = UserInfo();
    m_accessToken.clear();
    m_refreshToken.clear();
    m_userAvatar->setPixmap(QPixmap());
    m_userAvatar->setText(QStringLiteral("头"));
    m_userTitle->clear();
    m_userBar->setVisible(false);
    if (m_friendsPage) {
        m_stack->removeWidget(m_friendsPage);
        delete m_friendsPage;
        m_friendsPage = nullptr;
    }
    showForcedLogin();
}

// 登录后创建并常连 WebSocket；生命周期=登录态（回主菜单/进对局都不关闭）
void StartScreen::ensureInternet()
{
    if (m_accessToken.isEmpty() || !ServerConfig::isConfigured())
        return;
    if (!m_internet) {
        m_internet = new InternetClient(this);
        // 全局私信/礼物：好友页未打开（主菜单、对局中）也要能收到
        connect(m_internet, &InternetClient::dmReceived,
                this, &StartScreen::onGlobalDm, Qt::UniqueConnection);
        connect(m_internet, &InternetClient::giftReceived,
                this, &StartScreen::onGlobalGift, Qt::UniqueConnection);
        connect(m_internet, &InternetClient::friendRequestReceived,
                this, &StartScreen::onGlobalFriendRequest, Qt::UniqueConnection);
        // 全局对战邀请+匹配：任意页面（含对局中）都能收到并应答进对局
        connect(m_internet, &InternetClient::inviteReceived,
                this, &StartScreen::onGlobalInvite, Qt::UniqueConnection);
        connect(m_internet, &InternetClient::inviteDeclined,
                this, &StartScreen::onGlobalInviteDeclined, Qt::UniqueConnection);
        connect(m_internet, &InternetClient::matchReady,
                this, &StartScreen::onInternetMatched, Qt::UniqueConnection);
        // lambda 不能用 Qt::UniqueConnection（connect 会直接失败），创建分支内只连一次无需去重
        connect(m_internet, &InternetClient::socketClosed, this, [this]() {
            onInternetClosed();
            scheduleWsReconnect();
        });
    }
    m_wsWanted = true;
    if (!m_internet->isOpen())
        m_internet->open(m_accessToken);
}

void StartScreen::scheduleWsReconnect()
{
    if (m_wsWanted && m_internet && !m_internet->isOpen() && !m_wsRetry->isActive())
        m_wsRetry->start(1500);
}

// ===================== 开始界面背景音乐 =====================

// 懒创建播放器：QMediaPlayer/QAudioOutput 构造会同步初始化 Media Foundation，
// 不能放进构造函数（启动卡顿元凶，见开发笔记性能铁律）
void StartScreen::ensureBgmPlayer()
{
    if (m_bgmPlayer)
        return;
    m_bgmAudio = new QAudioOutput(this);
    m_bgmAudio->setVolume(0.4f);   // 音量适中
    m_bgmPlayer = new QMediaPlayer(this);
    m_bgmPlayer->setAudioOutput(m_bgmAudio);
    m_bgmPlayer->setLoops(QMediaPlayer::Infinite);   // 循环播放
    m_bgmPlayer->setSource(QUrl(QStringLiteral("qrc:/startmusic.wav")));
}

void StartScreen::startBgm()
{
    if (!m_bgmOn)
        return;
    if (m_bgmPlayer && m_bgmPlayer->playbackState() == QMediaPlayer::PlayingState)
        return;
    ensureBgmPlayer();
    m_bgmPlayer->play();
}

void StartScreen::stopBgm()
{
    if (m_bgmPlayer)
        m_bgmPlayer->stop();
}

void StartScreen::updateBgmBtnText()
{
    if (!m_bgmBtn)
        return;
    m_bgmBtn->setText(m_bgmOn ? QStringLiteral("🎵 音乐：开")
                              : QStringLiteral("🎵 音乐：关"));
}

// 窗口显示（启动/关对局回菜单）：稍等窗口渲染完再起音乐，避免音频初始化卡首帧
void StartScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_bgmOn)
        QTimer::singleShot(400, this, [this]() {
            if (isVisible() && m_bgmOn)
                startBgm();
        });
}

// 窗口隐藏（进对局 StartScreen 被关闭/替换）：立即停，防止新旧菜单双音乐
void StartScreen::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    stopBgm();
}

void StartScreen::onOpenProfile()
{
    ProfileDialog dlg(m_api, m_currentUser, m_accessToken, this);
    connect(&dlg, &ProfileDialog::profileUpdated, this, [this](const UserInfo &u) {
        m_currentUser = u;
        m_userName->setText(u.display_name);
        // 绑定手机号会改变头衔，同步刷新
        QString title = u.title;
        if (title.isEmpty())
            title = QStringLiteral("全服最拉");
        m_userTitle->setText(QStringLiteral("◇ %1").arg(title));
        if (title == QStringLiteral("全服第一"))
            m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#d4a017;font-weight:bold;"));
        else if (title == QStringLiteral("全服第二"))
            m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#1976d2;"));
        else
            m_userTitle->setStyleSheet(QStringLiteral("font-size:12px;color:#999;"));
        if (!u.avatar_url.isEmpty()) {
            connect(m_api, &ApiClient::avatarDownloaded, this, [this](const QString &, const QPixmap &pm) {
                QPixmap scaled = pm.scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                int x = (scaled.width() - 48) / 2, y = (scaled.height() - 48) / 2;
                QPixmap cropped = scaled.copy(x, y, 48, 48);
                QPixmap out(48, 48);
                out.fill(Qt::transparent);
                QPainter p(&out);
                p.setRenderHint(QPainter::Antialiasing, true);
                QPainterPath path;
                path.addEllipse(0, 0, 48, 48);
                p.setClipPath(path);
                p.drawPixmap(0, 0, cropped);
                p.end();
                m_userAvatar->setPixmap(out);
            }, Qt::SingleShotConnection);
            m_api->downloadAvatar(ServerConfig::serverUrl() + u.avatar_url);
        }
    });
    dlg.exec();
}

// ===================== 互联网对战（邀请好友开局） =====================

void StartScreen::onInternetMode()
{
    // 有没回完的对战邀请：点按钮先把邀请弹回来（红点提示的落地动作）
    if (!m_pendingInvites.isEmpty()) {
        promptInvite(*m_pendingInvites.constBegin());
        return;
    }
    if (!ServerConfig::isConfigured()) {
        QMessageBox::information(this, QStringLiteral("服务器地址"),
                                 QStringLiteral("请先点左下角「🌐 服务器」填写房主提供的地址"));
        onEditServerUrl();
        if (!ServerConfig::isConfigured())
            return;
    }
    ensureInternet();
    openFriendsPage(true);   // 互联网对局唯一开局方式：好友面板里邀请好友
}

void StartScreen::onFriends()
{
    // 好友入口：简洁好友页（聊天/送礼/资料），邀请对战走「互联网对战」入口
    openFriendsPage(false);
}

// 红点：邮件=新手礼包未领；好友=主菜单期间未读私信/礼物/好友请求数
void StartScreen::refreshBadges()
{
    if (m_mailBadge)
        m_mailBadge->setVisible(!MailDialog::welcomeClaimed());

    const int unread = QSettings().value(QStringLiteral("friendsUnread"), 0).toInt();
    if (m_friendsBadge) {
        if (unread > 0) {
            m_friendsBadge->setText(unread > 9 ? QStringLiteral("9+")
                                               : QString::number(unread));
            m_friendsBadge->setVisible(true);
        } else {
            m_friendsBadge->setVisible(false);
        }
    }

    // 待回应对战邀请红点
    if (m_inviteBadge)
        m_inviteBadge->setVisible(!m_pendingInvites.isEmpty());
}

void StartScreen::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    // 红点贴按钮右上角（子控件坐标）
    if (m_friendsBadge && m_friendsBtn)
        m_friendsBadge->move(m_friendsBtn->width() - 20, -4);
    if (m_mailBadge && m_mailBtn)
        m_mailBadge->move(m_mailBtn->width() - 14, 0);
    if (m_inviteBadge && m_internetBtn)
        m_inviteBadge->move(m_internetBtn->width() - 20, -4);
}

void StartScreen::onEditServerUrl()
{
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, QStringLiteral("服务器地址"),
        QStringLiteral("输入房主提供的服务器地址（http:// 或 https:// 开头）："),
        QLineEdit::Normal, ServerConfig::serverUrl(), &ok);
    if (!ok)
        return;
    const QString u = url.trimmed();
    if (u.isEmpty() ||
        (!u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
         !u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))) {
        QMessageBox::warning(this, QStringLiteral("服务器地址"),
                             QStringLiteral("地址需要以 http:// 或 https:// 开头"));
        return;
    }
    ServerConfig::setServerUrl(u);
    // 实时连接常连中：用新地址立刻重连（断线回调也会自动重连，这里立即触发）
    if (m_internet)
        m_internet->close();
    if (m_wsWanted)
        m_wsRetry->start(300);
    QMessageBox::information(this, QStringLiteral("服务器地址"),
                             QStringLiteral("已保存，正在使用新地址重新连接。"));
}

void StartScreen::openFriendsPage(bool allowInvite)
{
    if (!ServerConfig::isConfigured()) {
        QMessageBox::information(this, QStringLiteral("服务器地址"),
                                 QStringLiteral("请先点左下角「🌐 服务器」填写房主提供的地址"));
        onEditServerUrl();
        if (!ServerConfig::isConfigured())
            return;
    }

    // WS 常连复用：没连/已断就立即建立
    ensureInternet();

    // 每次进入新建好友页
    if (m_friendsPage) {
        m_stack->removeWidget(m_friendsPage);
        delete m_friendsPage;
        m_friendsPage = nullptr;
    }
    m_friendsPage = new FriendsPanel(m_api, m_internet, m_currentUser, m_accessToken,
                                     this, allowInvite);
    const int pageIdx = m_stack->addWidget(m_friendsPage);

    // 退出登录：真正关闭 WS 并清登录态；返回：只切回主菜单，WS 保持在线
    connect(m_friendsPage, &FriendsPanel::logout,
            this, &StartScreen::onLogout);
    connect(m_friendsPage, &FriendsPanel::back,
            this, [this]() {
        onInternetLogoutCleanup();
        m_stack->setCurrentIndex(0);
    });
    connect(m_internet, &InternetClient::helloReceived,
            m_friendsPage, &FriendsPanel::start);
    // matchReady 已在 ensureInternet 全局连接：邀请在任意页面接受都能直接进对局

    m_stack->setCurrentIndex(pageIdx);
    // WS 可能在进页面之前就完成握手了：直接先拉一次，hello 后还会再刷
    if (m_internet->isOpen())
        QTimer::singleShot(100, m_friendsPage, &FriendsPanel::start);

    // 进入好友页 = 已读，清红点
    QSettings().setValue(QStringLiteral("friendsUnread"), 0);
    refreshBadges();
}

void StartScreen::onInternetClosed()
{
    // 只有人正停在好友页时才弹断线提示；主菜单/对局中静默自动重连
    if (m_friendsPage && m_stack->currentWidget() == m_friendsPage) {
        QMessageBox::warning(this, QStringLiteral("连接断开"),
                             QStringLiteral("与服务器的连接已断开，正在自动重连…"));
    }
}

void StartScreen::onInternetLogoutCleanup()
{
    if (m_friendsPage) {
        m_stack->removeWidget(m_friendsPage);
        delete m_friendsPage;
        m_friendsPage = nullptr;
    }
}

// 收到对战邀请：任意页面弹窗应答；关掉/未答前红点亮着，可从「互联网对战」按钮重提
void StartScreen::onGlobalInvite(int fromUserId, const QString &fromName)
{
    m_pendingInvites.insert(fromUserId);
    m_pendingInviteNames.insert(fromUserId, fromName);
    refreshBadges();
    promptInvite(fromUserId);
}

void StartScreen::promptInvite(int fromUserId)
{
    const QString name = m_pendingInviteNames.value(fromUserId);
    // 父窗口用当前激活窗口：好友停在主菜单/其他页面/对局中，弹窗都跟到哪
    QWidget *parent = QApplication::activeWindow();
    if (!parent)
        parent = this;
    const auto ret = QMessageBox::question(
        parent, QStringLiteral("对战邀请"),
        QStringLiteral("%1 邀请你进行互联网对战，是否接受？").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    m_pendingInvites.remove(fromUserId);
    m_pendingInviteNames.remove(fromUserId);
    refreshBadges();
    if (m_internet && m_internet->isOpen())
        m_internet->respondInvite(fromUserId, ret == QMessageBox::Yes);
    else
        QMessageBox::warning(parent, QStringLiteral("网络错误"),
                             QStringLiteral("与服务器的连接已断开，无法回应邀请。"));
}

// 邀请方收到拒绝结果（原来只在好友页提示，现在全局可见）
void StartScreen::onGlobalInviteDeclined(int)
{
    QWidget *parent = QApplication::activeWindow();
    if (!parent)
        parent = this;
    QMessageBox::information(parent, QStringLiteral("邀请结果"),
                             QStringLiteral("对方拒绝了本次邀请"));
}

// 主菜单/对局中收到私信：好友页不在前台时弹出独立聊天窗
void StartScreen::onGlobalDm(int fromUserId, const QString &fromName,
                             const QString &text)
{
    // 好友页打开着：它自己的 onDmReceived 会处理，避免重复弹窗
    if (m_friendsPage && m_stack->currentWidget() == m_friendsPage)
        return;
    // 主菜单期间的未读消息：好友按钮红点 +1（对局中也记，回菜单可见）
    if (!text.isEmpty()) {
        QSettings st;
        st.setValue(QStringLiteral("friendsUnread"),
                    st.value(QStringLiteral("friendsUnread"), 0).toInt() + 1);
        refreshBadges();
    }
    ChatDialog *dlg = m_globalChats.value(fromUserId, nullptr);
    if (!dlg) {
        dlg = new ChatDialog(m_internet, fromUserId, fromName, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QObject::destroyed, this, [this, fromUserId]() {
            m_globalChats.remove(fromUserId);
        });
        m_globalChats.insert(fromUserId, dlg);
    }
    if (!text.isEmpty())
        dlg->receiveMessage(fromName, text);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// 主菜单/对局中收到礼物（含对方离线时服务端暂存、上线补送的礼物）
void StartScreen::onGlobalGift(int fromUserId, const QString &fromName,
                               const QString &text)
{
    Q_UNUSED(fromUserId);
    if (m_friendsPage && m_stack->currentWidget() == m_friendsPage)
        return;  // 好友页自己会弹
    // 入背包（含糖果计数/礼物清单/总数兼容）
    Inventory::addGift(fromName, text);
    const Inventory::Gift g = Inventory::gifts().value(0);
    QSettings st;
    st.setValue(QStringLiteral("friendsUnread"),
                st.value(QStringLiteral("friendsUnread"), 0).toInt() + 1);
    refreshBadges();
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("收到礼物"));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
                    "<div align='center'>"
                    "<span style='font-size:46px;'>%1</span><br>"
                    "<b style='font-size:16px;'>%2</b> 送你一份%3<br>"
                    "<span style='color:#888;'>已放入背包，去「🎒 背包」查看吧</span>"
                    "</div>")
                    .arg(g.emoji.toHtmlEscaped(),
                         fromName.toHtmlEscaped(),
                         g.name.toHtmlEscaped()));
    box.exec();
}

// 主菜单/对局中收到好友请求：不弹窗打扰，好友按钮亮红点
void StartScreen::onGlobalFriendRequest(int requestId, const UserInfo &from,
                                        const QString &message)
{
    Q_UNUSED(requestId);
    Q_UNUSED(from);
    Q_UNUSED(message);
    if (m_friendsPage && m_stack->currentWidget() == m_friendsPage)
        return;
    QSettings st;
    st.setValue(QStringLiteral("friendsUnread"),
                st.value(QStringLiteral("friendsUnread"), 0).toInt() + 1);
    refreshBadges();
}

PlayerCardInfo StartScreen::selfCard() const
{
    PlayerCardInfo c;
    c.valid = true;
    c.isSelf = true;
    c.name = m_currentUser.display_name.isEmpty()
        ? m_currentUser.username : m_currentUser.display_name;
    c.title = m_currentUser.title;
    c.avatarUrl = m_currentUser.avatar_url;
    c.avatarSeed = m_currentUser.id;
    return c;
}

void StartScreen::onInternetMatched(const QString &roomId, const QString &side,
                                    const UserInfo &opp)
{
    Q_UNUSED(roomId)
    NetworkManager *net = new NetworkManager(this);
    net->attachInternet(m_internet);
    const bool isHost = (side == QLatin1String("red"));

    // 对手名片（match_ready 已带头像/头衔）
    PlayerCardInfo oppCard;
    oppCard.valid = true;
    oppCard.name = opp.display_name.isEmpty() ? opp.username : opp.display_name;
    oppCard.title = opp.title;
    oppCard.avatarUrl = opp.avatar_url;
    oppCard.avatarSeed = opp.id;

    m_game = new MainWindow(MainWindow::GameMode::Online, isHost, net, isHost,
                            false, -1, 0, selfCard(), oppCard);
    net->setParent(m_game);
    m_game->show();
    m_game->raise();
    m_game->activateWindow();
    // 注意：不关闭主菜单（也不 close WS）——隐藏在后面保持实时通道常连，
    // 对局结束回到的新主菜单会接管并自动重连
}

// ===================== 单机 / 局域网 =====================

void StartScreen::onStartSingle()
{
    // 开局前确认：AI 难度 + 红黑方，确认后才进棋盘
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("人机对战设置"));
    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;

    auto *levelCombo = new QComboBox(&dlg);
    levelCombo->addItem(QStringLiteral("笨糖果（AI 思考 10 秒）"));
    levelCombo->addItem(QStringLiteral("一般糖果（AI 思考 15 秒）"));
    levelCombo->addItem(QStringLiteral("聪明糖果（AI 思考 20 秒）"));
    form->addRow(QStringLiteral("AI 难度："), levelCombo);

    auto *sideCombo = new QComboBox(&dlg);
    sideCombo->addItem(QStringLiteral("执红（先手）"));
    sideCombo->addItem(QStringLiteral("执黑（后手）"));
    form->addRow(QStringLiteral("执子："), sideCombo);

    v->addLayout(form);
    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("开始对战"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_game = new MainWindow(MainWindow::GameMode::SinglePlayer, false,
                            nullptr,
                            sideCombo->currentIndex() == 0,   // 执红？
                            false, -1,
                            levelCombo->currentIndex(),      // 难度档位
                            selfCard(), PlayerCardInfo());
    m_game->show();
    close();
}

// ===================== 残局挑战 =====================

// 选择残局：列表 + 目标说明 + 杀法提示，确认后进入对局（步数限制内将杀黑方）
void StartScreen::onPickEndgame()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("残局挑战"));
    dlg.setFixedWidth(420);
    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(18, 16, 18, 14);
    lay->setSpacing(10);

    auto *tip = new QLabel(QStringLiteral("你执红先行，在限定步数内将杀黑方即挑战成功。"), &dlg);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("font-size:13px;color:#666;"));
    lay->addWidget(tip);

    auto *list = new QListWidget(&dlg);
    list->setStyleSheet(QStringLiteral(
        "QListWidget{background:white;border:1px solid #e0e0e0;border-radius:6px;}"
        "QListWidget::item{padding:7px 10px;}"
        "QListWidget::item:selected{background:#eef4fb;color:#333;}"));
    for (int i = 0; i < endgameCount(); ++i)
        list->addItem(QStringLiteral("%1. %2").arg(i + 1)
                          .arg(QString::fromUtf8(endgameInfo(i).name)));
    // 固定列表高度：QListWidget 的 minimumSizeHint 会撑爆布局把按钮顶出对话框，
    // 固定后由布局按实际内容自适应整窗高度
    list->setFixedHeight(270);
    lay->addWidget(list);

    auto *goalLbl = new QLabel(&dlg);
    goalLbl->setWordWrap(true);
    goalLbl->setMinimumHeight(28);  // wordWrap 的 minimumSizeHint 只算一行，会裁掉布局，须手动占位
    goalLbl->setStyleSheet(QStringLiteral("font-size:14px;font-weight:bold;color:#333;"));
    auto *hintLbl = new QLabel(&dlg);
    hintLbl->setWordWrap(true);
    hintLbl->setMinimumHeight(56);  // 预留两行，防止把按钮行顶出对话框
    hintLbl->setStyleSheet(QStringLiteral("font-size:13px;color:#666;"));
    lay->addWidget(goalLbl);
    lay->addWidget(hintLbl);

    auto *btnRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(QStringLiteral("取消"), &dlg);
    auto *startBtn = new QPushButton(QStringLiteral("开始挑战"), &dlg);
    startBtn->setStyleSheet(QStringLiteral(
        "background:#495057;color:white;border:none;border-radius:6px;"
        "padding:8px 22px;font-size:14px;"));
    cancelBtn->setStyleSheet(QStringLiteral(
        "background:#f1f3f5;color:#495057;border:1px solid #e0e0e0;"
        "border-radius:6px;padding:8px 18px;"));
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(startBtn);
    lay->addLayout(btnRow);

    connect(list, &QListWidget::currentRowChanged, &dlg,
            [goalLbl, hintLbl](int row) {
        if (row < 0) return;
        const EndgameInfo &info = endgameInfo(row);
        goalLbl->setText(QStringLiteral("🎯 目标：%1").arg(QString::fromUtf8(info.goal)));
        hintLbl->setText(QStringLiteral("💡 提示：%1").arg(QString::fromUtf8(info.hint)));
    });
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(startBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    list->setCurrentRow(0);

    if (dlg.exec() != QDialog::Accepted)
        return;
    const int idx = list->currentRow();
    if (idx < 0)
        return;
    m_game = new MainWindow(MainWindow::GameMode::SinglePlayer, false,
                            nullptr, true, false, idx, 0,
                            selfCard(), PlayerCardInfo());
    m_game->show();
    close();
}

void StartScreen::onShowOnline()
{
    m_stack->setCurrentIndex(1);
}

void StartScreen::onBackToMenu()
{
    if (m_net) {
        m_net->deleteLater();
        m_net = nullptr;
    }
    m_stack->setCurrentIndex(0);
}

void StartScreen::onCreateRoom()
{
    m_isHost = true;
    m_isSpectator = false;
    m_hostIsRed = QRandomGenerator::global()->bounded(2) == 0; // 随机红黑方
    m_roomCode = QString::number(QRandomGenerator::global()->bounded(1000, 10000));
    const quint16 port = roomPort(m_roomCode);

    m_net = new NetworkManager(this);
    connect(m_net, &NetworkManager::connected, this, &StartScreen::onConnected);
    connect(m_net, &NetworkManager::errorOccurred, this, &StartScreen::onNetError);

    m_statusLabel->setText(
        QStringLiteral("<div style='font-size:52px;font-weight:bold;color:#e8590c;'>%1</div>"
                       "<div style='margin-top:8px;'>房间号</div>")
        .arg(m_roomCode));
    m_stack->setCurrentIndex(2);
    m_net->startHosting(port, m_roomCode);
}

void StartScreen::onJoinPage()
{
    m_isHost = false;
    m_isSpectator = false;
    m_roomList->clear();
    m_codeInput->clear();
    m_stack->setCurrentIndex(3);

    if (!m_net) {
        m_net = new NetworkManager(this);
        connect(m_net, &NetworkManager::connected, this, &StartScreen::onConnected);
        connect(m_net, &NetworkManager::errorOccurred, this, &StartScreen::onNetError);
        connect(m_net, &NetworkManager::roomFound, this, &StartScreen::onRoomFound);
        connect(m_net, &NetworkManager::roomsCleared, this, &StartScreen::onRoomsCleared);
    }
    m_net->startDiscovery();
}

void StartScreen::onRoomFound(const QString &code)
{
    m_roomList->addItem(QStringLiteral("房间号  %1（等待加入）").arg(code));
    m_roomList->sortItems();
}

void StartScreen::onRoomsCleared()
{
    m_roomList->clear();
}

void StartScreen::onJoinRoom()
{
    if (!m_net)
        return;

    QString code;
    QListWidgetItem *cur = m_roomList->currentItem();
    if (!cur && !m_roomList->selectedItems().isEmpty())
        cur = m_roomList->selectedItems().first();
    if (cur) {
        const QString text = cur->text();
        for (const QChar &ch : text) {
            if (ch.isDigit())
                code += ch;
        }
        if (code.length() > 4)
            code = code.left(4);
    }
    if (code.isEmpty())
        code = m_codeInput->text().trimmed();

    if (code.length() != 4 || !code.toUInt()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请选择房间，或输入4位房间号"));
        return;
    }

    const auto rooms = m_net->rooms();
    if (!rooms.contains(code)) {
        QMessageBox::warning(this, QStringLiteral("未找到房间"),
                             QStringLiteral("没有扫描到房间号 %1。\n"
                                            "请确认房主已创建房间，且双方在同一局域网/WiFi").arg(code));
        return;
    }

    const NetworkManager::RoomInfo info = rooms.value(code);
    m_statusLabel->setText(QStringLiteral("正在加入房间 %1 …").arg(code));
    m_stack->setCurrentIndex(2);
    m_net->connectToHost(info.ip.toString(), info.port);
}

void StartScreen::onSpectateJoin()
{
    if (!m_net)
        return;

    m_isSpectator = true;
    m_isHost = false;

    QString code;
    QListWidgetItem *cur = m_roomList->currentItem();
    if (!cur && !m_roomList->selectedItems().isEmpty())
        cur = m_roomList->selectedItems().first();
    if (cur) {
        const QString text = cur->text();
        for (const QChar &ch : text) {
            if (ch.isDigit())
                code += ch;
        }
        if (code.length() > 4)
            code = code.left(4);
    }
    if (code.isEmpty())
        code = m_codeInput->text().trimmed();

    if (code.length() != 4 || !code.toUInt()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请选择房间，或输入4位房间号"));
        return;
    }

    const auto rooms = m_net->rooms();
    if (!rooms.contains(code)) {
        QMessageBox::warning(this, QStringLiteral("未找到房间"),
                             QStringLiteral("没有扫描到房间号 %1。\n"
                                            "请确认房主已创建房间，且双方在同一局域网/WiFi").arg(code));
        return;
    }

    const NetworkManager::RoomInfo info = rooms.value(code);
    m_statusLabel->setText(QStringLiteral("正在观战加入房间 %1 …").arg(code));
    m_stack->setCurrentIndex(2);
    m_net->connectToHost(info.ip.toString(), info.port);
}

void StartScreen::onConnected()
{
    m_net->disconnect(this);
    if (m_isHost) {
        // 房主：立即通知加入者执方，进游戏
        m_net->sendSide(m_hostIsRed);
        m_game = new MainWindow(MainWindow::GameMode::Online, m_isHost, m_net,
                                 m_hostIsRed, false, -1, 0,
                                 selfCard(), PlayerCardInfo());
        m_net->setParent(m_game);
        m_net = nullptr;
        m_game->show();
        close();
    } else if (m_isSpectator) {
        // 观战者：等待房主发送 SPEC 确认
        connect(m_net, &NetworkManager::spectatorReceived, this, &StartScreen::onSpectatorConfirmed);
        QTimer::singleShot(5000, this, [this]() {
            if (!m_game)
                QMessageBox::warning(this, QStringLiteral("无法观战"),
                    QStringLiteral("该房间尚未开始对局，请等对局者加入后再观战"));
        });
    } else {
        // 对局加入者：等待房主发送 SIDE 执方信息
        connect(m_net, &NetworkManager::sideReceived, this, &StartScreen::onSideReceived);
        QTimer::singleShot(5000, this, [this]() {
            if (!m_game)
                QMessageBox::warning(this, QStringLiteral("连接超时"),
                    QStringLiteral("未收到房主执方信息，请确认房主已创建房间"));
        });
    }
}

void StartScreen::onSideReceived(bool hostIsRed)
{
    // 加入者收到执方：房主红则我黑，房主黑则我红
    const bool myRed = !hostIsRed;
    m_game = new MainWindow(MainWindow::GameMode::Online, m_isHost, m_net,
                            myRed, false, -1, 0,
                            selfCard(), PlayerCardInfo());
    m_net->setParent(m_game);
    m_net = nullptr;
    m_game->show();
    close();
}

void StartScreen::onSpectatorConfirmed()
{
    // 观战者收到 SPEC 确认：以观战模式进游戏
    m_game = new MainWindow(MainWindow::GameMode::Online, m_isHost, m_net,
                            true, true, -1, 0,
                            PlayerCardInfo(), PlayerCardInfo());
    m_net->setParent(m_game);
    m_net = nullptr;
    m_game->show();
    close();
}

void StartScreen::onNetError(const QString &msg)
{
    QMessageBox::critical(this, QStringLiteral("连接失败"), msg);
    onBackToMenu();
}
