#include "BagDialog.h"

#include "Inventory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScrollArea>
#include <QHash>
#include <QPair>

static QLabel *makeSectionTitle(const QString &emoji, const QString &text, QWidget *p)
{
    auto *lbl = new QLabel(QStringLiteral("%1 %2").arg(emoji, text), p);
    lbl->setStyleSheet(QStringLiteral(
        "font-size:18px;font-weight:bold;color:#333;border:none;"));
    return lbl;
}

BagDialog::BagDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("我的背包"));
    resize(480, 680);
    rebuild();
}

void BagDialog::rebuild()
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 16, 20, 16);
    v->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("我的背包"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:24px;font-weight:bold;color:#333;"));
    v->addWidget(title);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *holder = new QWidget();
    auto *lay = new QVBoxLayout(holder);
    lay->setContentsMargins(2, 2, 8, 2);
    lay->setSpacing(10);

    // ===== 货币 =====
    auto *moneyCard = new QFrame(holder);
    moneyCard->setStyleSheet(QStringLiteral(
        "QFrame{background:white;border:1px solid #e0e0e0;border-radius:8px;}"));
    auto *ml = new QHBoxLayout(moneyCard);
    ml->setContentsMargins(18, 14, 18, 14);
    auto *mIcon = new QLabel(QStringLiteral("💰"), moneyCard);
    mIcon->setStyleSheet(QStringLiteral("font-size:36px;border:none;"));
    ml->addWidget(mIcon);
    auto *mText = new QLabel(
        QStringLiteral("<div style='color:#868e96;font-size:15px;'>我的货币</div>"
                       "<div style='color:#333;font-size:26px;font-weight:bold;'>%1￥</div>")
            .arg(Inventory::money()), moneyCard);
    mText->setStyleSheet(QStringLiteral("border:none;"));
    ml->addWidget(mText, 1);
    lay->addWidget(moneyCard);

    // ===== 糖果 =====
    lay->addWidget(makeSectionTitle(QStringLiteral("🍬"),
                                    QStringLiteral("糖果（对局中召唤指导棋）"), holder));
    static const struct { const char *emoji; const char *name; } kCandies[] = {
        { "🍭", "笨糖果" }, { "🍬", "一般糖果" }, { "🍫", "聪明糖果" },
    };
    for (int i = 0; i < 3; ++i) {
        auto *card = new QFrame(holder);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:white;border:1px solid #e0e0e0;border-radius:8px;}"));
        auto *cl = new QHBoxLayout(card);
        cl->setContentsMargins(14, 8, 14, 8);
        auto *e = new QLabel(QString::fromUtf8(kCandies[i].emoji), card);
        e->setStyleSheet(QStringLiteral("font-size:26px;border:none;"));
        cl->addWidget(e);
        auto *n = new QLabel(QString::fromUtf8(kCandies[i].name), card);
        n->setStyleSheet(QStringLiteral("font-size:15px;color:#333;border:none;"));
        cl->addWidget(n, 1);
        auto *cnt = new QLabel(QStringLiteral("x %1").arg(Inventory::candyCount(i)), card);
        cnt->setStyleSheet(QStringLiteral(
            "font-size:18px;font-weight:bold;color:#333;border:none;"));
        cl->addWidget(cnt);
        lay->addWidget(card);
    }

    // ===== 道具 =====
    lay->addWidget(makeSectionTitle(QStringLiteral("✨"),
                                    QStringLiteral("特殊道具"), holder));
    {
        const int n = Inventory::itemCount(QStringLiteral("mimimimi"));
        auto *card = new QFrame(holder);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:white;border:1px solid %1;border-radius:8px;}")
            .arg(n > 0 ? QStringLiteral("#868e96") : QStringLiteral("#e0e0e0")));
        auto *il = new QHBoxLayout(card);
        il->setContentsMargins(14, 10, 14, 10);
        auto *e = new QLabel(QStringLiteral("🤡"), card);
        e->setStyleSheet(QStringLiteral("font-size:30px;border:none;"));
        il->addWidget(e);
        auto *info = new QVBoxLayout;
        auto *name = new QLabel(QStringLiteral("mimimimi"), card);
        name->setStyleSheet(QStringLiteral(
            "font-size:17px;font-weight:bold;color:#333;border:none;"));
        info->addWidget(name);
        auto *desc = new QLabel(QStringLiteral("神秘商品——买了也没啥用，但你已经有了。"), card);
        desc->setStyleSheet(QStringLiteral("font-size:12px;color:#868e96;border:none;"));
        info->addWidget(desc);
        il->addLayout(info, 1);
        auto *cnt = new QLabel(n > 0 ? QStringLiteral("x %1").arg(n)
                                     : QStringLiteral("未拥有"), card);
        cnt->setStyleSheet(QStringLiteral(
            "font-size:16px;font-weight:bold;color:%1;border:none;")
            .arg(n > 0 ? QStringLiteral("#333") : QStringLiteral("#adb5bd")));
        il->addWidget(cnt);
        lay->addWidget(card);
    }

    // ===== 好友礼物 =====
    lay->addWidget(makeSectionTitle(QStringLiteral("🎁"),
                                    QStringLiteral("好友送的礼物"), holder));
    const QList<Inventory::Gift> gifts = Inventory::gifts();
    if (gifts.isEmpty()) {
        auto *empty = new QLabel(
            QStringLiteral("还没有收到礼物，去「好友」里找小伙伴要一份吧～"), holder);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral("font-size:13px;color:#adb5bd;border:none;"));
        empty->setWordWrap(true);
        lay->addWidget(empty);
    } else {
        // 同名礼物聚合计数，展示 收到N份/最近赠送者
        struct Agg { int count = 0; QString emoji; QString name; QString lastFrom; QString lastTime; };
        QHash<QString, Agg> map;
        QList<QString> order;
        for (const auto &g : gifts) {
            const QString key = g.emoji + g.name;
            if (!map.contains(key)) {
                Agg a; a.emoji = g.emoji; a.name = g.name;
                a.lastFrom = g.from; a.lastTime = g.time;
                map.insert(key, a);
                order.append(key);
            }
            Agg &a = map[key];
            a.count++;
            a.lastFrom = g.from;  // gifts() 已按最新在前
            a.lastTime = g.time;
        }
        for (const QString &key : order) {
            const Agg &a = map.value(key);
            auto *card = new QFrame(holder);
            card->setStyleSheet(QStringLiteral(
                "QFrame{background:white;border:1px solid #e0e0e0;border-radius:8px;}"));
            auto *gl = new QHBoxLayout(card);
            gl->setContentsMargins(14, 8, 14, 8);
            auto *e = new QLabel(a.emoji, card);
            e->setStyleSheet(QStringLiteral("font-size:26px;border:none;"));
            gl->addWidget(e);
            auto *info = new QVBoxLayout;
            auto *n = new QLabel(QStringLiteral("%1").arg(a.name), card);
            n->setStyleSheet(QStringLiteral(
                "font-size:15px;font-weight:bold;color:#333;border:none;"));
            info->addWidget(n);
            auto *meta = new QLabel(
                QStringLiteral("最近：%1 赠送 · %2").arg(a.lastFrom, a.lastTime), card);
            meta->setStyleSheet(QStringLiteral("font-size:11px;color:#868e96;border:none;"));
            info->addWidget(meta);
            gl->addLayout(info, 1);
            auto *cnt = new QLabel(QStringLiteral("x %1").arg(a.count), card);
            cnt->setStyleSheet(QStringLiteral(
                "font-size:17px;font-weight:bold;color:#333;border:none;"));
            gl->addWidget(cnt);
            lay->addWidget(card);
        }
    }

    lay->addStretch();
    scroll->setWidget(holder);
    v->addWidget(scroll, 1);

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setMinimumHeight(40);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#495057;color:white;border:none;border-radius:8px;"
        "font-size:16px;}"
        "QPushButton:hover{background:#343a40;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);
}
