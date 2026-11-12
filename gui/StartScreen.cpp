#include "StartScreen.h"
#include "MainWindow.h"
#include "NetworkManager.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QStackedWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QRandomGenerator>

// 房间号 1000~9999 映射到 TCP 端口 31000~39999
quint16 StartScreen::roomPort(const QString &code)
{
    return static_cast<quint16>(30000 + code.toUInt());
}

StartScreen::StartScreen(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("糖果象棋"));
    resize(480, 520);

    m_stack = new QStackedWidget(this);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(60, 40, 60, 40);
    root->addWidget(m_stack);

    // ===== 第0页：主菜单 =====
    auto *menu = new QWidget();
    auto *menuLayout = new QVBoxLayout(menu);
    auto *title = new QLabel(QStringLiteral(" 糖果象棋3.1"), menu);
    title->setAlignment(Qt::AlignCenter);
    QFont f = title->font();
    f.setPointSize(28);
    f.setBold(true);
    title->setFont(f);
    menuLayout->addWidget(title);
    menuLayout->addSpacing(40);

    m_singleBtn = new QPushButton(QStringLiteral("开始游戏"), menu);
    m_onlineBtn = new QPushButton(QStringLiteral("联机模式"), menu);
    for (auto *b : {m_singleBtn, m_onlineBtn}) {
        b->setMinimumHeight(56);
        b->setStyleSheet(QStringLiteral("font-size:18px;"));
        menuLayout->addWidget(b);
    }
    menuLayout->addStretch();
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
    auto *backFromJoin = new QPushButton(QStringLiteral("← 返回"), join);
    joinLayout->addWidget(joinTitle);
    joinLayout->addSpacing(8);
    joinLayout->addWidget(m_roomList, 3);
    joinLayout->addSpacing(8);
    joinLayout->addWidget(m_codeInput);
    joinLayout->addSpacing(12);
    joinLayout->addWidget(connectBtn);
    joinLayout->addWidget(backFromJoin);
    m_stack->addWidget(join);

    // ===== 信号连接 =====
    connect(m_singleBtn, &QPushButton::clicked, this, &StartScreen::onStartSingle);
    connect(m_onlineBtn, &QPushButton::clicked, this, &StartScreen::onShowOnline);
    connect(m_backBtn, &QPushButton::clicked, this, &StartScreen::onBackToMenu);
    connect(m_createBtn, &QPushButton::clicked, this, &StartScreen::onCreateRoom);
    connect(m_joinBtn, &QPushButton::clicked, this, &StartScreen::onJoinPage);
    connect(connectBtn, &QPushButton::clicked, this, &StartScreen::onJoinRoom);
    connect(m_roomList, &QListWidget::itemDoubleClicked, this, &StartScreen::onJoinRoom);
    connect(backFromJoin, &QPushButton::clicked, this, &StartScreen::onBackToMenu);
    connect(cancelBtn, &QPushButton::clicked, this, &StartScreen::onBackToMenu);
}

void StartScreen::onStartSingle()
{
    m_game = new MainWindow(MainWindow::GameMode::SinglePlayer, false);
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

// 房主：随机4位房间号，TCP 端口与房间号绑定，UDP 广播
void StartScreen::onCreateRoom()
{
    m_isHost = true;
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

// 进入加入页：开始自动扫描
void StartScreen::onJoinPage()
{
    m_isHost = false;
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
    // 保持房间号升序
    m_roomList->sortItems();
}

void StartScreen::onRoomsCleared()
{
    m_roomList->clear();
}

// 加入房间：优先列表选中项，其次手动输入的房间号
void StartScreen::onJoinRoom()
{
    if (!m_net)
        return;

    QString code;
    QListWidgetItem *cur = m_roomList->currentItem();
    if (!cur && !m_roomList->selectedItems().isEmpty())
        cur = m_roomList->selectedItems().first();
    if (cur) {
        // 列表文字形如 "房间号  1234（等待加入）"，提取4位数字
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

void StartScreen::onConnected()
{
    // 连接成功 → 进入游戏，把网络管理器所有权转交给 MainWindow
    // 断开本页的信号槽：之后的棋步/断连/错误只归游戏窗口管，
    // 避免对局中弹出父窗口是本页（已隐藏）的"连接失败"框
    m_net->disconnect(this);
    m_game = new MainWindow(MainWindow::GameMode::Online, m_isHost, m_net);
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
