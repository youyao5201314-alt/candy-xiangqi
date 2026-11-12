// 奶龙表情选择器 + 聊天消息渲染（对局聊天与好友私信共用）
#include "StickerPicker.h"

#include <QGridLayout>
#include <QWidgetAction>
#include <QToolButton>
#include <QTextEdit>
#include <QPixmap>
#include <QRegularExpression>
#include <QStringList>
#include <QTabWidget>
#include <QScrollArea>
#include <QPushButton>

static const QStringList kStickerIds = {
    QStringLiteral("s01"), QStringLiteral("s02"), QStringLiteral("s03"),
    QStringLiteral("s04"), QStringLiteral("s05"), QStringLiteral("s06"),
    QStringLiteral("s07"), QStringLiteral("s08"), QStringLiteral("s09"),
    QStringLiteral("s10"), QStringLiteral("s11")
};

// emoji 表情库（点击直接作为聊天文本发送）
static const QList<QPair<QString, QStringList>> kEmojiTabs = {
    { QStringLiteral("表情"), {
        QStringLiteral("😀"), QStringLiteral("😃"), QStringLiteral("😄"),
        QStringLiteral("😁"), QStringLiteral("😆"), QStringLiteral("😅"),
        QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😊"),
        QStringLiteral("😍"), QStringLiteral("😘"), QStringLiteral("😜"),
        QStringLiteral("🤪"), QStringLiteral("😎"), QStringLiteral("🤩"),
        QStringLiteral("🥳"), QStringLiteral("😏"), QStringLiteral("😒"),
        QStringLiteral("😞"), QStringLiteral("😔"), QStringLiteral("😢"),
        QStringLiteral("😭"), QStringLiteral("😱"), QStringLiteral("😡"),
        QStringLiteral("😴"), QStringLiteral("🤔"), QStringLiteral("🤗"),
        QStringLiteral("🤭"), QStringLiteral("🥺"), QStringLiteral("😤"),
        QStringLiteral("😈"), QStringLiteral("👿"), QStringLiteral("🤡"),
        QStringLiteral("👻"), QStringLiteral("💀"), QStringLiteral("👽"),
        QStringLiteral("🤖"), QStringLiteral("🎃"), QStringLiteral("😺"),
        QStringLiteral("😹"), QStringLiteral("😻"), QStringLiteral("🙈"),
        QStringLiteral("🙉"), QStringLiteral("🙊"), QStringLiteral("👍"),
    } },
    { QStringLiteral("手势"), {
        QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("👏"),
        QStringLiteral("🙏"), QStringLiteral("💪"), QStringLiteral("✌️"),
        QStringLiteral("🤝"), QStringLiteral("👌"), QStringLiteral("✊"),
        QStringLiteral("👊"), QStringLiteral("✋"), QStringLiteral("👋"),
        QStringLiteral("🤙"), QStringLiteral("☝️"), QStringLiteral("👆"),
        QStringLiteral("👇"), QStringLiteral("👈"), QStringLiteral("👉"),
        QStringLiteral("🖐️"), QStringLiteral("🖖"), QStringLiteral("🤞"),
        QStringLiteral("🤟"), QStringLiteral("🤘"), QStringLiteral("👐"),
    } },
    { QStringLiteral("心情"), {
        QStringLiteral("❤️"), QStringLiteral("🧡"), QStringLiteral("💛"),
        QStringLiteral("💚"), QStringLiteral("💙"), QStringLiteral("💜"),
        QStringLiteral("🖤"), QStringLiteral("🤍"), QStringLiteral("💔"),
        QStringLiteral("💖"), QStringLiteral("💯"), QStringLiteral("✨"),
        QStringLiteral("⭐"), QStringLiteral("🎉"), QStringLiteral("🎊"),
        QStringLiteral("🔥"), QStringLiteral("❓"), QStringLiteral("❗"),
        QStringLiteral("💤"), QStringLiteral("💢"), QStringLiteral("💦"),
        QStringLiteral("🍀"), QStringLiteral("🌈"), QStringLiteral("☀️"),
        QStringLiteral("🌙"), QStringLiteral("⚡"), QStringLiteral("🎵"),
    } },
    { QStringLiteral("吃喝玩乐"), {
        QStringLiteral("🐲"), QStringLiteral("🐶"), QStringLiteral("🐱"),
        QStringLiteral("🐭"), QStringLiteral("🐹"), QStringLiteral("🐰"),
        QStringLiteral("🦊"), QStringLiteral("🐻"), QStringLiteral("🐼"),
        QStringLiteral("🐨"), QStringLiteral("🐯"), QStringLiteral("🦁"),
        QStringLiteral("🐮"), QStringLiteral("🐷"), QStringLiteral("🐸"),
        QStringLiteral("🐵"), QStringLiteral("🐔"), QStringLiteral("🐧"),
        QStringLiteral("🦄"), QStringLiteral("🍭"), QStringLiteral("🍬"),
        QStringLiteral("🍫"), QStringLiteral("🍰"), QStringLiteral("🎂"),
        QStringLiteral("🍦"), QStringLiteral("🍪"), QStringLiteral("🍕"),
        QStringLiteral("🍔"), QStringLiteral("🍟"), QStringLiteral("🍗"),
        QStringLiteral("🍺"), QStringLiteral("🧋"), QStringLiteral("🎁"),
        QStringLiteral("🌹"), QStringLiteral("🏆"), QStringLiteral("👑"),
    } },
};

StickerPicker::StickerPicker(QWidget *parent)
    : QMenu(parent)
{
    setStyleSheet(QStringLiteral(
        "QMenu{background:white;border:1px solid #d0bfff;border-radius:10px;}"));
    setMinimumWidth(380);

    auto *tabs = new QTabWidget(this);
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane{border:1px solid #e5dbff;border-radius:6px;top:-1px;}"
        "QTabBar::tab{background:#f3f0ff;color:#5f3dc4;padding:6px 14px;"
        "border-top-left-radius:8px;border-top-right-radius:8px;font-size:13px;}"
        "QTabBar::tab:selected{background:#7048e8;color:white;}"));

    // ----- 奶龙贴纸页（可滚动网格） -----
    auto *stickerScroll = new QScrollArea(tabs);
    stickerScroll->setWidgetResizable(true);
    stickerScroll->setFrameShape(QFrame::NoFrame);
    stickerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *grid = new QWidget();
    auto *lay = new QGridLayout(grid);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    for (int i = 0; i < kStickerIds.size(); i++) {
        const QString id = kStickerIds[i];
        auto *btn = new QToolButton(grid);
        btn->setFixedSize(72, 72);
        btn->setIconSize(QSize(66, 66));
        btn->setIcon(QIcon(QStringLiteral(":/stickers/%1.jpg").arg(id)));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{border:1px solid #eee;border-radius:8px;background:#fff;}"
            "QToolButton:hover{border:2px solid #7048e8;background:#f3f0ff;}"));
        connect(btn, &QToolButton::clicked, this, [this, id]() {
            emit picked(id);
            close();
        });
        lay->addWidget(btn, i / 4, i % 4);
    }
    stickerScroll->setWidget(grid);
    tabs->addTab(stickerScroll, QStringLiteral("🐲 奶龙 x%1").arg(kStickerIds.size()));

    // ----- emoji 各分类页 -----
    for (const auto &tab : kEmojiTabs) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *holder = new QWidget();
        auto *gl = new QGridLayout(holder);
        gl->setContentsMargins(8, 8, 8, 8);
        gl->setSpacing(4);
        const QStringList emojis = tab.second;
        const int cols = 7;
        for (int i = 0; i < emojis.size(); i++) {
            const QString e = emojis[i];
            auto *btn = new QToolButton(holder);
            btn->setFixedSize(44, 44);
            btn->setText(e);
            btn->setStyleSheet(QStringLiteral(
                "QToolButton{font-size:24px;border:none;border-radius:8px;}"
                "QToolButton:hover{background:#f3f0ff;}"));
            connect(btn, &QToolButton::clicked, this, [this, e]() {
                emit emojiPicked(e);
                close();
            });
            gl->addWidget(btn, i / cols, i % cols);
        }
        scroll->setWidget(holder);
        tabs->addTab(scroll, tab.first);
    }

    auto *action = new QWidgetAction(this);
    action->setDefaultWidget(tabs);
    addAction(action);
}

QString StickerPicker::stickerId(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^\\[sticker:(s\\d{2})\\]$"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

void StickerPicker::appendMessage(QTextEdit *view, const QString &whoHtml,
                                  const QString &text)
{
    if (!view)
        return;
    const QString id = stickerId(text);
    if (!id.isEmpty()) {
        // 表情独占一行，96×96 显示
        view->append(whoHtml);
        view->append(QStringLiteral("<img src=\"qrc:/stickers/%1.jpg\" width=\"96\" height=\"96\">")
                         .arg(id));
    } else {
        view->append(whoHtml + text.toHtmlEscaped());
    }
}
