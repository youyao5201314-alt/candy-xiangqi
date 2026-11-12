// 糖果商城对话框实现：用对局货币购买糖果（消耗品，对局中可让 AI 代走一步）
#include "ShopDialog.h"

#include "Inventory.h"
#include "Achievements.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QSettings>
#include <QMessageBox>

// 糖果定义：索引即 QSettings candyN 的 N
struct CandyDef {
    const char *emoji;
    const char *name;
    const char *desc;
    int price;
    const char *cardColor;
};
static const CandyDef CANDIES[] = {
    { "🍭", "笨糖果",   "指导棋：AI 思考 10 秒替你走一步", 333,  "#ffffff" },
    { "🍬", "一般糖果", "指导棋：AI 思考 15 秒替你走一步", 666, "#ffffff" },
    { "🍫", "聪明糖果", "指导棋：AI 思考 20 秒替你走一步", 888, "#ffffff" },
};
static const int CANDY_COUNT = 3;

ShopDialog::ShopDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("🍬 糖果商城"));
    resize(460, 640);
    setupUi();
}

int ShopDialog::money() const
{
    return QSettings().value(QStringLiteral("money"), 0).toInt();
}

int ShopDialog::candyCount(int idx) const
{
    return QSettings().value(QStringLiteral("candy%1").arg(idx), 0).toInt();
}

void ShopDialog::setupUi()
{
    auto *v = new QVBoxLayout(this);
    v->setSpacing(12);
    v->setContentsMargins(24, 20, 24, 20);

    // ===== 标题 =====
    auto *title = new QLabel(QStringLiteral("🍬 糖果商城"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size:24px;font-weight:bold;color:#333;"));
    v->addWidget(title);

    // ===== 余额 + 糖果库存 =====
    m_balanceLbl = new QLabel(this);
    m_balanceLbl->setAlignment(Qt::AlignCenter);
    m_balanceLbl->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:bold;color:#333;"));
    v->addWidget(m_balanceLbl);

    v->addSpacing(4);

    // ===== 糖果卡片 =====
    for (int i = 0; i < CANDY_COUNT; i++) {
        const CandyDef &c = CANDIES[i];
        auto *card = new QFrame(this);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:%1;border:1px solid #e0e0e0;border-radius:10px;}")
            .arg(QString::fromUtf8(c.cardColor)));
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);

        auto *emojiLbl = new QLabel(QString::fromUtf8(c.emoji), card);
        emojiLbl->setStyleSheet(QStringLiteral("font-size:40px;"));
        emojiLbl->setAlignment(Qt::AlignCenter);
        emojiLbl->setMinimumWidth(56);
        cardLayout->addWidget(emojiLbl);

        auto *infoLayout = new QVBoxLayout;
        infoLayout->setSpacing(4);
        auto *nameLbl = new QLabel(QString::fromUtf8(c.name), card);
        nameLbl->setStyleSheet(QStringLiteral(
            "font-size:18px;font-weight:bold;color:#333;border:none;"));
        infoLayout->addWidget(nameLbl);

        auto *descLbl = new QLabel(QString::fromUtf8(c.desc), card);
        descLbl->setStyleSheet(QStringLiteral("font-size:13px;color:#666;border:none;"));
        descLbl->setWordWrap(true);
        infoLayout->addWidget(descLbl);

        // 库存数量
        auto *countLbl = new QLabel(card);
        countLbl->setStyleSheet(QStringLiteral("font-size:12px;color:#868e96;border:none;"));
        m_countLbls[i] = countLbl;
        infoLayout->addWidget(countLbl);

        cardLayout->addLayout(infoLayout, 1);

        // 购买按钮
        auto *buyBtn = new QPushButton(
            QStringLiteral("%1￥ 购买").arg(c.price), card);
        buyBtn->setMinimumWidth(96);
        buyBtn->setMinimumHeight(36);
        buyBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#495057;color:white;border:none;border-radius:6px;}"
            "QPushButton:hover{background:#343a40;}"));
        connect(buyBtn, &QPushButton::clicked, this, [this, i]() { buy(i); });
        cardLayout->addWidget(buyBtn);

        v->addWidget(card);
    }

    // ===== 特殊商品：mimimimi（买了也没啥用，可在背包查看） =====
    {
        auto *card = new QFrame(this);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame{background:white;"
            "border:1px dashed #adb5bd;border-radius:8px;}"));
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);

        auto *emojiLbl = new QLabel(QStringLiteral("🤡"), card);
        emojiLbl->setStyleSheet(QStringLiteral("font-size:40px;border:none;"));
        emojiLbl->setAlignment(Qt::AlignCenter);
        emojiLbl->setMinimumWidth(56);
        cardLayout->addWidget(emojiLbl);

        auto *infoLayout = new QVBoxLayout;
        infoLayout->setSpacing(4);
        auto *nameLbl = new QLabel(QStringLiteral("mimimimi"), card);
        nameLbl->setStyleSheet(QStringLiteral(
            "font-size:18px;font-weight:bold;color:#333;border:none;"));
        infoLayout->addWidget(nameLbl);
        auto *descLbl = new QLabel(
            QStringLiteral("神秘商品。买了也没啥用，但可以在背包里查看。"), card);
        descLbl->setStyleSheet(QStringLiteral("font-size:13px;color:#666;border:none;"));
        descLbl->setWordWrap(true);
        infoLayout->addWidget(descLbl);
        auto *ownLbl = new QLabel(card);
        ownLbl->setStyleSheet(QStringLiteral("font-size:12px;color:#868e96;border:none;"));
        ownLbl->setText(QStringLiteral("已拥有：x%1")
            .arg(Inventory::itemCount(QStringLiteral("mimimimi"))));
        infoLayout->addWidget(ownLbl);
        cardLayout->addLayout(infoLayout, 1);

        auto *buyBtn = new QPushButton(QStringLiteral("10000￥ 购买"), card);
        buyBtn->setMinimumWidth(104);
        buyBtn->setMinimumHeight(36);
        buyBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#495057;color:white;border:none;border-radius:6px;}"
            "QPushButton:hover{background:#343a40;}"));
        connect(buyBtn, &QPushButton::clicked, this, [this, ownLbl]() {
            if (!Inventory::spendMoney(10000)) {
                QMessageBox::information(this, QStringLiteral("余额不足"),
                    QStringLiteral("mimimimi 要 10000￥，再去赢几把奶娃吧！"));
                return;
            }
            Inventory::addItem(QStringLiteral("mimimimi"), 1);
            ownLbl->setText(QStringLiteral("已拥有：x%1")
                .arg(Inventory::itemCount(QStringLiteral("mimimimi"))));
            const QStringList notices = Achievements::recordEvent(
                QStringLiteral("mimi"), 1);
            QString extra;
            if (!notices.isEmpty())
                extra = QStringLiteral("\n\n🏆 成就解锁：\n%1").arg(notices.join(QStringLiteral("\n")));
            QMessageBox::information(this, QStringLiteral("购买成功"),
                QStringLiteral("你买下了 mimimimi！\n虽然没啥用，但它已经躺在你的背包里了。%1")
                    .arg(extra));
            refresh();
        });
        cardLayout->addWidget(buyBtn);
        v->addWidget(card);
    }

    v->addStretch();

    // ===== 底部说明 =====
    auto *note = new QLabel(
        QStringLiteral("人机对战结算：胜 +2000￥ / 平 +1000￥ / 负 +500￥\n"
                       "糖果为一次性消耗品：对局中点「糖果指导」让 AI 替你走一步\n"
                       "可用模式：人机对战、局域网（仅房客）、互联网对战"), this);
    note->setAlignment(Qt::AlignCenter);
    note->setStyleSheet(QStringLiteral("font-size:12px;color:#999;"));
    note->setWordWrap(true);
    v->addWidget(note);

    v->addSpacing(4);

    // ===== 关闭按钮 =====
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setMinimumHeight(40);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#495057;color:white;border:none;border-radius:6px;"
        "font-size:16px;}"
        "QPushButton:hover{background:#343a40;}"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    v->addWidget(closeBtn);

    refresh();
}

// 刷新余额和库存显示
void ShopDialog::refresh()
{
    m_balanceLbl->setText(QStringLiteral("余额：%1￥").arg(money()));
    for (int i = 0; i < CANDY_COUNT; i++) {
        if (m_countLbls.value(i))
            m_countLbls[i]->setText(
                QStringLiteral("剩余：x%1").arg(candyCount(i)));
    }
}

// 购买糖果（消耗品 +1）
void ShopDialog::buy(int idx)
{
    const CandyDef &c = CANDIES[idx];
    QSettings st;
    const int cur = st.value(QStringLiteral("money"), 0).toInt();
    if (cur < c.price) {
        QMessageBox::information(this, QStringLiteral("余额不足"),
            QStringLiteral("还差 %1￥ 才能买到%2，去赢几把吧！")
                .arg(c.price - cur)
                .arg(QString::fromUtf8(c.name)));
        return;
    }
    st.setValue(QStringLiteral("money"), cur - c.price);
    const QString key = QStringLiteral("candy%1").arg(idx);
    st.setValue(key, st.value(key, 0).toInt() + 1);
    QMessageBox::information(this, QStringLiteral("购买成功"),
        QStringLiteral(" %1 +1！\n对局中点「糖果指导」即可让它替你走一步。")
            .arg(QString::fromUtf8(c.name)));
    refresh();
}
