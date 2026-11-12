// 邮件对话框实现：新手礼包邮件，领取 10000￥ 启动资金（一次性）
#include "MailDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QSettings>
#include <QMessageBox>

static const int MAIL_AWARD = 10000;

MailDialog::MailDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("📮 邮件"));
    resize(420, 460);
    setupUi();
}

bool MailDialog::welcomeClaimed()
{
    return QSettings().value(QStringLiteral("welcomeMailClaimed"), false).toBool();
}

bool MailDialog::claimed() const
{
    return welcomeClaimed();
}

void MailDialog::setupUi()
{
    auto *v = new QVBoxLayout(this);
    v->setSpacing(12);
    v->setContentsMargins(24, 20, 24, 20);

    // ===== 标题 =====
    auto *title = new QLabel(QStringLiteral("📮 邮件"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:24px;font-weight:bold;color:#e8590c;"));
    v->addWidget(title);

    v->addSpacing(4);

    // ===== 邮件卡片 =====
    auto *card = new QFrame(this);
    card->setFrameShape(QFrame::StyledPanel);
    card->setStyleSheet(QStringLiteral(
        "QFrame{background:#fffbf0;border:1px solid #ffe0a3;border-radius:10px;}"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto *mailTitle = new QLabel(QStringLiteral("🎁 新手礼包"), card);
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

    v->addStretch();

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
        m_stateLbl->setText(QStringLiteral("🎁 奖励待领取：%1￥").arg(MAIL_AWARD));
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
    QSettings st;
    const int cur = st.value(QStringLiteral("money"), 0).toInt();
    st.setValue(QStringLiteral("money"), cur + MAIL_AWARD);
    st.setValue(QStringLiteral("welcomeMailClaimed"), true);
    QMessageBox::information(this, QStringLiteral("领取成功"),
        QStringLiteral("🎉 10000￥ 已到账！\n当前余额：%1￥").arg(cur + MAIL_AWARD));
    refresh();
}
