#include "AchievementsDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QMessageBox>

AchievementsDialog::AchievementsDialog(ApiClient *api, const QString &accessToken,
                                       const QString &currentTitle, QWidget *parent)
    : QDialog(parent), m_api(api), m_token(accessToken), m_title(currentTitle)
{
    setWindowTitle(QStringLiteral("🏆 成就与称号"));
    resize(480, 660);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("🏆 成就殿堂"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:24px;font-weight:bold;color:#333;"));
    v->addWidget(title);

    auto *sub = new QLabel(
        QStringLiteral("击败奶娃、打出精彩杀法、互联网取胜都能解锁成就\n"
                       "成就奖励货币自动到账，解锁的称号可以装备（名片与个人主页可见）"), this);
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet(QStringLiteral("font-size:12px;color:#888;"));
    sub->setWordWrap(true);
    v->addWidget(sub);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *holder = new QWidget();
    auto *cards = new QVBoxLayout(holder);
    cards->setContentsMargins(2, 2, 8, 2);
    cards->setSpacing(8);

    for (const Achievements::Def &d : Achievements::defs()) {
        const bool got = Achievements::isUnlocked(d.id);
        const int cur = qMin(Achievements::progress(d.stat), d.target);

        auto *card = new QFrame(holder);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:white;border:1px solid %1;border-radius:8px;}")
            .arg(got ? QStringLiteral("#868e96") : QStringLiteral("#e0e0e0")));
        auto *row = new QHBoxLayout(card);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(10);

        auto *icon = new QLabel(d.icon, card);
        icon->setStyleSheet(QStringLiteral("font-size:30px;border:none;"));
        icon->setFixedWidth(44);
        icon->setAlignment(Qt::AlignCenter);
        row->addWidget(icon);

        auto *info = new QVBoxLayout;
        info->setSpacing(2);
        auto *name = new QLabel(
            (got ? QStringLiteral("✅ ") : QString()) + d.name, card);
        name->setStyleSheet(QStringLiteral(
            "font-size:16px;font-weight:bold;color:%1;border:none;")
            .arg(got ? QStringLiteral("#333") : QStringLiteral("#adb5bd")));
        info->addWidget(name);
        auto *desc = new QLabel(d.desc, card);
        desc->setStyleSheet(QStringLiteral("font-size:12px;color:#666;border:none;"));
        info->addWidget(desc);
        auto *prog = new QLabel(
            QStringLiteral("进度 %1/%2").arg(cur).arg(d.target), card);
        prog->setStyleSheet(QStringLiteral("font-size:12px;color:#868e96;border:none;"));
        info->addWidget(prog);
        row->addLayout(info, 1);

        if (d.reward > 0 || !d.title.isEmpty()) {
            auto *rew = new QLabel(
                (d.reward > 0
                    ? QStringLiteral("💰%1￥\n").arg(d.reward)
                    : QString())
                + (!d.title.isEmpty()
                    ? QStringLiteral("称号「%1」").arg(d.title)
                    : QString()), card);
            rew->setAlignment(Qt::AlignCenter);
            rew->setStyleSheet(QStringLiteral(
                "font-size:12px;color:#868e96;border:none;"));
            row->addWidget(rew);
        }

        // 称号装备/卸下
        if (!d.title.isEmpty()) {
            auto *eqBtn = new QPushButton(card);
            eqBtn->setMinimumWidth(72);
            if (!got) {
                eqBtn->setText(QStringLiteral("未解锁"));
                eqBtn->setEnabled(false);
                eqBtn->setStyleSheet(QStringLiteral(
                    "QPushButton{background:#ced4da;color:#666;border:none;"
                    "border-radius:6px;padding:6px;}"));
            } else if (m_title == d.title) {
                eqBtn->setText(QStringLiteral("卸下"));
                eqBtn->setStyleSheet(QStringLiteral(
                    "QPushButton{background:white;color:#495057;"
                    "border:1px solid #adb5bd;border-radius:6px;padding:6px;}"
                    "QPushButton:hover{border-color:#495057;}"));
                connect(eqBtn, &QPushButton::clicked, this, [this]() { equip(QString()); });
            } else {
                eqBtn->setText(QStringLiteral("装备"));
                eqBtn->setStyleSheet(QStringLiteral(
                    "QPushButton{background:#495057;color:white;border:none;"
                    "border-radius:6px;padding:6px;}"
                    "QPushButton:hover{background:#343a40;}"));
                connect(eqBtn, &QPushButton::clicked, this, [this, d]() { equip(d.title); });
            }
            row->addWidget(eqBtn);
        }
        cards->addWidget(card);
    }
    cards->addStretch();
    scroll->setWidget(holder);
    v->addWidget(scroll, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setMinimumHeight(38);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#495057;color:white;border:none;border-radius:8px;"
        "font-size:16px;}"
        "QPushButton:hover{background:#343a40;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);

    connect(m_api, &ApiClient::profileUpdated, this, &AchievementsDialog::onProfileUpdated);
    connect(m_api, &ApiClient::errorOccurred, this, &AchievementsDialog::onApiError);
}

void AchievementsDialog::equip(const QString &title)
{
    Achievements::setEquippedTitle(title);
    m_api->setTitle(m_token, title);
}

void AchievementsDialog::onProfileUpdated(const UserInfo &user)
{
    m_title = user.title;
    emit titleChanged(user);
    QMessageBox::information(this, QStringLiteral("称号"),
        m_title.isEmpty()
            ? QStringLiteral("已卸下成就称号，恢复默认头衔。")
            : QStringLiteral("已装备称号：%1").arg(m_title));
    accept();
}

void AchievementsDialog::onApiError(const QString &msg)
{
    QMessageBox::warning(this, QStringLiteral("操作失败"), msg);
}
