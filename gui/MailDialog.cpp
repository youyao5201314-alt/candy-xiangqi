// 邮件对话框实现：本地新手礼包 + 服务端系统邮件（货币/糖果附件领取）
#include "MailDialog.h"
#include "api.h"
#include "Inventory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QSettings>
#include <QMessageBox>

static const int MAIL_AWARD = 10000;

MailDialog::MailDialog(QWidget *parent, ApiClient *api, const QString &accessToken)
    : QDialog(parent), m_api(api), m_token(accessToken)
{
    setWindowTitle(QStringLiteral("邮件"));
    resize(440, 560);
    setupUi();

    // 有登录态：拉取服务端系统邮件
    if (m_api && !m_token.isEmpty()) {
        connect(m_api, &ApiClient::mailsOk, this, &MailDialog::onMails);
        connect(m_api, &ApiClient::mailClaimOk, this, &MailDialog::onClaimOk);
        m_api->listMails(m_token);
    }
}

bool MailDialog::welcomeClaimed()
{
    return QSettings().value(QStringLiteral("welcomeMailClaimed"), false).toBool();
}

bool MailDialog::claimed() const
{
    return welcomeClaimed();
}

// 附件文案
QString MailDialog::attachText(const QString &kind, int amount)
{
    if (kind == QStringLiteral("coins"))
        return QStringLiteral("💰 %1￥").arg(amount);
    if (kind == QStringLiteral("candy0"))
        return QStringLiteral("🍬 笨糖果 ×%1").arg(amount);
    if (kind == QStringLiteral("candy1"))
        return QStringLiteral("🍬 一般糖果 ×%1").arg(amount);
    if (kind == QStringLiteral("candy2"))
        return QStringLiteral("🍬 聪明糖果 ×%1").arg(amount);
    return QString();
}

void MailDialog::setupUi()
{
    auto *v = new QVBoxLayout(this);
    v->setSpacing(10);
    v->setContentsMargins(24, 16, 24, 16);

    // ===== 标题 =====
    auto *title = new QLabel(QStringLiteral("邮件"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:22px;font-weight:bold;color:#e8590c;"));
    v->addWidget(title);

    // ===== 服务端系统邮件区（可滚动） =====
    auto *srvHeader = new QLabel(QStringLiteral("📮 系统邮件"), this);
    srvHeader->setStyleSheet(QStringLiteral("font-size:15px;font-weight:bold;color:#495057;"));
    v->addWidget(srvHeader);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    auto *host = new QWidget(scroll);
    m_serverList = new QVBoxLayout(host);
    m_serverList->setContentsMargins(0, 0, 0, 0);
    m_serverList->setSpacing(8);

    m_serverEmpty = new QLabel(QStringLiteral("暂无系统邮件"), host);
    m_serverEmpty->setAlignment(Qt::AlignCenter);
    m_serverEmpty->setStyleSheet(QStringLiteral("color:#adb5bd;font-size:13px;"));
    m_serverList->addWidget(m_serverEmpty);
    m_serverList->addStretch();
    scroll->setWidget(host);
    v->addWidget(scroll, 1);

    if (!m_api || m_token.isEmpty()) {
        srvHeader->setVisible(false);
        scroll->setVisible(false);
    }

    // ===== 本地新手礼包卡 =====
    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QStringLiteral(
        "QFrame{background:#fffbf0;border:1px solid #ffe0a3;border-radius:10px;}"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto *mailTitle = new QLabel(QStringLiteral("新手礼包"), card);
    mailTitle->setStyleSheet(QStringLiteral(
        "font-size:18px;font-weight:bold;color:#333;border:none;"));
    cardLayout->addWidget(mailTitle);

    auto *mailBody = new QLabel(
        QStringLiteral("欢迎来到糖果象棋！\n\n"
                       "这是给你的启动资金：\n"
                       "💰 10000￥\n\n"
                       "去商城买几颗糖果，对局中可以召唤指导棋哦～\n\n"
                       "—— yoyo"), card);
    mailBody->setStyleSheet(QStringLiteral(
        "font-size:14px;color:#555;border:none;line-height:1.6;"));
    mailBody->setWordWrap(true);
    cardLayout->addWidget(mailBody);

    cardLayout->addStretch();

    // 领取按钮 / 已领取状态
    auto *claimRow = new QHBoxLayout;
    m_stateLbl = new QLabel(card);
    m_stateLbl->setStyleSheet(QStringLiteral("font-size:14px;color:#999;border:none;"));
    claimRow->addWidget(m_stateLbl, 1);

    m_claimBtn = new QPushButton(card);
    m_claimBtn->setMinimumSize(110, 40);
    connect(m_claimBtn, &QPushButton::clicked, this, &MailDialog::claim);
    claimRow->addWidget(m_claimBtn);

    cardLayout->addLayout(claimRow);
    v->addWidget(card);

    // ===== 关闭按钮 =====
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setMinimumHeight(40);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#ff8787;color:white;border:none;border-radius:6px;"
        "font-size:16px;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);

    refresh();
}

void MailDialog::refresh()
{
    if (claimed()) {
        m_stateLbl->setText(QStringLiteral("✅ 奖励已领取"));
        m_claimBtn->setText(QStringLiteral("已领取"));
        m_claimBtn->setEnabled(false);
        m_claimBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#ced4da;color:#666;border:none;border-radius:6px;}"));
    } else {
        m_stateLbl->setText(QStringLiteral("奖励待领取：%1￥").arg(MAIL_AWARD));
        m_claimBtn->setText(QStringLiteral("领取"));
        m_claimBtn->setEnabled(true);
        m_claimBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#ffd43b;color:#333;border:none;border-radius:6px;"
            "font-weight:bold;}"
            "QPushButton:hover{background:#fcc419;}"));
    }
}

void MailDialog::claim()
{
    // 走 Inventory::addMoney：登录态下会经同步钩子上报服务器
    const int before = Inventory::money();
    Inventory::addMoney(MAIL_AWARD);
    QSettings().setValue(QStringLiteral("welcomeMailClaimed"), true);
    QMessageBox::information(this, QStringLiteral("领取成功"),
        QStringLiteral("🎉 10000￥ 已到账！\n当前余额：%1￥").arg(before + MAIL_AWARD));
    refresh();
}

// ---------- 服务端邮件 ----------

void MailDialog::onMails(const QList<MailInfo> &mails)
{
    m_mails = mails;
    rebuildServerMails();
}

void MailDialog::rebuildServerMails()
{
    if (!m_serverList)
        return;

    // 先摘出占位标签（不销毁，可复用），再清空旧卡片（保留末尾 stretch）
    if (m_serverEmpty)
        m_serverList->removeWidget(m_serverEmpty);
    while (m_serverList->count() > 1) {
        QLayoutItem *it = m_serverList->takeAt(0);
        if (auto *w = it->widget())
            w->deleteLater();
        delete it;
    }

    if (m_mails.isEmpty()) {
        m_serverList->insertWidget(0, m_serverEmpty);
        m_serverEmpty->setVisible(true);
        return;
    }
    m_serverEmpty->setVisible(false);

    // 最新的在前
    for (int i = m_mails.size() - 1; i >= 0; --i) {
        const MailInfo &m = m_mails.at(i);

        auto *card = new QFrame(this);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:#ffffff;border:1px solid #e0e0e0;border-radius:10px;}"));
        auto *lay = new QVBoxLayout(card);
        lay->setContentsMargins(14, 10, 14, 10);
        lay->setSpacing(6);

        auto *head = new QHBoxLayout;
        auto *t = new QLabel(m.title, card);
        t->setStyleSheet(QStringLiteral(
            "font-size:15px;font-weight:bold;color:#333;border:none;"));
        head->addWidget(t, 1);
        auto *date = new QLabel(m.createdAt, card);
        date->setStyleSheet(QStringLiteral("font-size:11px;color:#adb5bd;border:none;"));
        head->addWidget(date);
        lay->addLayout(head);

        if (!m.body.isEmpty()) {
            auto *body = new QLabel(m.body, card);
            body->setStyleSheet(QStringLiteral(
                "font-size:13px;color:#555;border:none;"));
            body->setWordWrap(true);
            lay->addWidget(body);
        }

        const QString attach = attachText(m.attachKind, m.attachAmount);
        auto *foot = new QHBoxLayout;
        auto *state = new QLabel(card);
        state->setStyleSheet(QStringLiteral("font-size:13px;color:#999;border:none;"));
        foot->addWidget(state, 1);

        auto *btn = new QPushButton(card);
        btn->setMinimumSize(88, 32);
        if (attach.isEmpty()) {
            // 无附件：无需领取
            state->setText(QString());
            btn->setText(QStringLiteral("已读"));
            btn->setEnabled(false);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton{background:#ced4da;color:#666;border:none;border-radius:6px;}"));
        } else if (m.claimed) {
            state->setText(QStringLiteral("附件：%1（已领取）").arg(attach));
            btn->setText(QStringLiteral("已领取"));
            btn->setEnabled(false);
            btn->setStyleSheet(QStringLiteral(
                "QPushButton{background:#ced4da;color:#666;border:none;border-radius:6px;}"));
        } else {
            state->setText(QStringLiteral("附件：%1").arg(attach));
            btn->setText(QStringLiteral("领取"));
            btn->setStyleSheet(QStringLiteral(
                "QPushButton{background:#ffd43b;color:#333;border:none;border-radius:6px;"
                "font-weight:bold;}"
                "QPushButton:hover{background:#fcc419;}"));
            const int mailId = m.id;
            connect(btn, &QPushButton::clicked, this, [this, mailId]() {
                if (m_api && !m_token.isEmpty())
                    m_api->claimMail(m_token, mailId);
            });
        }
        foot->addWidget(btn);
        lay->addLayout(foot);

        m_serverList->insertWidget(m_serverList->count() - 1, card);
    }
}

void MailDialog::onClaimOk(int mailId, int money, int candy0, int candy1, int candy2)
{
    Inventory::setFromServer(money, candy0, candy1, candy2);
    for (MailInfo &m : m_mails) {
        if (m.id == mailId)
            m.claimed = true;
    }
    rebuildServerMails();
    QMessageBox::information(this, QStringLiteral("领取成功"),
        QStringLiteral("🎉 附件已到账！\n当前余额：%1￥").arg(Inventory::money()));
}
