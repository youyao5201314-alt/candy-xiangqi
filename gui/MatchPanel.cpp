#include "MatchPanel.h"

#include "InternetClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

MatchPanel::MatchPanel(InternetClient *net, QWidget *parent)
    : QWidget(parent), m_net(net)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(40, 30, 40, 30);
    v->setSpacing(18);

    auto *logo = new QLabel(QStringLiteral(" "), this);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral("font-size:72px;"));
    v->addWidget(logo);

    auto *title = new QLabel(QStringLiteral("互联网快速匹配"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:26px;font-weight:bold;color:#5f3dc4;"));
    v->addWidget(title);

    m_state = new QLabel(QStringLiteral("准备好和全网棋手过招了吗？"), this);
    m_state->setAlignment(Qt::AlignCenter);
    m_state->setStyleSheet(QStringLiteral("font-size:16px;color:#495057;"));
    m_state->setWordWrap(true);
    v->addWidget(m_state);

    m_hint = new QLabel(
        QStringLiteral("点击开始后系统会为你随机匹配在线对手，\n"
                       "红黑方随机分配，匹配成功直接开局。"), this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet(QStringLiteral("font-size:13px;color:#868e96;"));
    m_hint->setWordWrap(true);
    v->addWidget(m_hint);

    v->addStretch();

    m_actionBtn = new QPushButton(QStringLiteral("开始匹配"), this);
    m_actionBtn->setMinimumHeight(64);
    m_actionBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #845ef7,stop:1 #5f3dc4);color:white;border:none;"
        "border-radius:14px;font-size:22px;font-weight:bold;}"
        "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #9775fa,stop:1 #7048e8);}"));
    connect(m_actionBtn, &QPushButton::clicked, this, &MatchPanel::toggleQueue);
    v->addWidget(m_actionBtn);

    auto *backBtn = new QPushButton(QStringLiteral("← 返回主菜单"), this);
    backBtn->setMinimumHeight(44);
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#e9ecef;color:#495057;border:none;"
        "border-radius:10px;font-size:16px;}"));
    connect(backBtn, &QPushButton::clicked, this, [this]() {
        leavePage();
        emit back();
    });
    v->addWidget(backBtn);

    connect(m_net, &InternetClient::matchReady,
            this, &MatchPanel::onMatchReady);
    connect(m_net, &InternetClient::serverError,
            this, &MatchPanel::onServerError);
}

void MatchPanel::setQueued(bool q)
{
    m_queued = q;
    if (q) {
        m_state->setText(QStringLiteral("⏳ 正在为你寻找对手…"));
        m_hint->setText(QStringLiteral("匹配期间请保持此页面打开"));
        m_actionBtn->setText(QStringLiteral("取消匹配"));
        m_actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #ff8787,stop:1 #fa5252);color:white;border:none;"
            "border-radius:14px;font-size:22px;font-weight:bold;}"));
    } else {
        m_state->setText(QStringLiteral("准备好和全网棋手过招了吗？"));
        m_hint->setText(QStringLiteral("点击开始后系统会为你随机匹配在线对手，\n"
                                       "红黑方随机分配，匹配成功直接开局。"));
        m_actionBtn->setText(QStringLiteral("🚀 开始匹配"));
        m_actionBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #845ef7,stop:1 #5f3dc4);color:white;border:none;"
            "border-radius:14px;font-size:22px;font-weight:bold;}"
            "QPushButton:hover{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #9775fa,stop:1 #7048e8);}"));
    }
}

void MatchPanel::toggleQueue()
{
    if (!m_net || !m_net->isOpen()) {
        m_state->setText(QStringLiteral("未连接服务器，正在自动重连，请稍候再试…"));
        return;
    }
    if (m_queued) {
        m_net->cancelMatchQueue();
        setQueued(false);
    } else {
        m_net->joinMatchQueue();
        setQueued(true);
    }
}

void MatchPanel::leavePage()
{
    if (m_queued && m_net)
        m_net->cancelMatchQueue();
    setQueued(false);
}

void MatchPanel::onMatchReady(const QString &roomId, const QString &side,
                              const UserInfo &opponent)
{
    if (!m_queued)
        return;  // 不是本页面发起的快速匹配（可能是好友邀请）
    setQueued(false);
    emit matched(roomId, side, opponent);
}

void MatchPanel::onServerError(const QString &code, const QString &msg)
{
    if (code == QLatin1String("match_queue")) {
        setQueued(false);
        m_state->setText(QStringLiteral("匹配失败：%1").arg(msg));
    }
}
