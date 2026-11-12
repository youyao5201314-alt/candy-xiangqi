// 好友私信对话框实现
#include "ChatDialog.h"
#include "InternetClient.h"
#include "StickerPicker.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>

ChatDialog::ChatDialog(InternetClient *net, int friendId, const QString &friendName,
                       QWidget *parent)
    : QDialog(parent), m_net(net), m_friendId(friendId), m_friendName(friendName)
{
    setWindowTitle(QStringLiteral("私信 — %1").arg(friendName));
    resize(380, 480);

    auto *v = new QVBoxLayout(this);
    m_view = new QTextEdit(this);
    m_view->setReadOnly(true);
    v->addWidget(m_view, 1);

    auto *inputRow = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入私信内容，回车发送…"));
    inputRow->addWidget(m_input, 1);

    auto *stickerBtn = new QPushButton(QStringLiteral("🐲"), this);
    stickerBtn->setFixedWidth(40);
    stickerBtn->setStyleSheet(QStringLiteral("font-size:18px;"));
    stickerBtn->setToolTip(QStringLiteral("奶龙表情"));
    inputRow->addWidget(stickerBtn);

    auto *sendBtn = new QPushButton(QStringLiteral("发送"), this);
    inputRow->addWidget(sendBtn);
    v->addLayout(inputRow);

    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        sendText(m_input->text().trimmed());
    });
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        sendText(m_input->text().trimmed());
    });
    connect(stickerBtn, &QPushButton::clicked, this, [this, stickerBtn]() {
        auto *picker = new StickerPicker(this);
        connect(picker, &StickerPicker::picked, this, [this](const QString &id) {
            sendText(QStringLiteral("[sticker:%1]").arg(id));
        });
        connect(picker, &StickerPicker::emojiPicked, this, [this](const QString &e) {
            sendText(e);
        });
        picker->exec();
        picker->deleteLater();
    });
}

void ChatDialog::sendText(const QString &text)
{
    if (text.isEmpty() || !m_net)
        return;
    m_net->sendDm(m_friendId, text);
    StickerPicker::appendMessage(
        m_view, QStringLiteral("<b style='color:#27ae60'>我：</b>"), text);
    m_input->clear();
}

void ChatDialog::receiveMessage(const QString &fromName, const QString &text)
{
    StickerPicker::appendMessage(
        m_view,
        QStringLiteral("<b style='color:#c0392b'>%1：</b>").arg(fromName.toHtmlEscaped()),
        text);
}
