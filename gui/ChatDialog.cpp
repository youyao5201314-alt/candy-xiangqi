// 好友私信对话框实现
#include "ChatDialog.h"
#include "InternetClient.h"
#include "StickerPicker.h"
#include "api.h"
#include "ServerConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QPainter>
#include <QPainterPath>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

static const int DM_HISTORY_MAX = 200;
static const int AVATAR_SIZE = 44;

static QString dmHistKey(int myId, int friendId)
{
    return QStringLiteral("dmHist_%1_%2").arg(myId).arg(friendId);
}

// 已获取的头像缓存（好友id → 圆形头像），跨窗口复用
static QHash<int, QPixmap> s_avatars;

// 圆形裁剪头像
static QPixmap avatarCircle(const QPixmap &src, int size)
{
    QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    const int x = (scaled.width() - size) / 2, y = (scaled.height() - size) / 2;
    const QPixmap cropped = scaled.copy(x, y, size, size);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, cropped);
    p.end();
    return out;
}

// 无自定义头像时：按 id 取色的彩色圆底 + 名字首字
static QPixmap avatarDefault(const QString &name, int id, int size)
{
    static const QColor palette[] = {
        QColor(255, 138, 128), QColor(129, 199, 132), QColor(129, 212, 250),
        QColor(206, 147, 216), QColor(255, 213, 79),  QColor(255, 171, 145),
        QColor(120, 204, 205), QColor(240, 152, 190)
    };
    const QColor base = palette[qAbs(id) % 8];
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(base);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(size * 18 / 36);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, name.left(1).toUpper());
    p.end();
    return out;
}

ChatDialog::ChatDialog(InternetClient *net, int myId, int friendId,
                       const QString &friendName, QWidget *parent,
                       const QString &avatarUrl, ApiClient *api)
    : QDialog(parent), m_net(net), m_myId(myId), m_friendId(friendId),
      m_friendName(friendName)
{
    setWindowTitle(QStringLiteral("私信 — %1").arg(friendName));
    resize(380, 500);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(12, 10, 12, 10);
    v->setSpacing(8);

    // ===== 顶部名片条：圆形头像 + 昵称 =====
    auto *head = new QHBoxLayout();
    m_avatarLbl = new QLabel(this);
    m_avatarLbl->setFixedSize(AVATAR_SIZE, AVATAR_SIZE);
    head->addWidget(m_avatarLbl);

    auto *nameLbl = new QLabel(friendName, this);
    nameLbl->setStyleSheet(QStringLiteral(
        "font-size:16px;font-weight:bold;color:#333;border:none;background:transparent;"));
    head->addWidget(nameLbl, 1);
    v->addLayout(head);

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

    // ===== 头像：缓存 → 下载 → 默认彩色首字 =====
    if (s_avatars.contains(friendId)) {
        m_avatarLbl->setPixmap(s_avatars.value(friendId));
    } else {
        m_avatarLbl->setPixmap(avatarDefault(friendName, friendId, AVATAR_SIZE));
        if (api && !avatarUrl.isEmpty()) {
            m_avatarUrl = ServerConfig::serverUrl() + avatarUrl;
            connect(api, &ApiClient::avatarDownloaded, this,
                    [this](const QString &url, const QPixmap &pm) {
                if (url != m_avatarUrl || pm.isNull())
                    return;
                const QPixmap c = avatarCircle(pm, AVATAR_SIZE);
                s_avatars.insert(m_friendId, c);
                m_avatarLbl->setPixmap(c);
            });
            api->downloadAvatar(m_avatarUrl);
        }
    }

    loadHistory();
}

void ChatDialog::sendText(const QString &text)
{
    if (text.isEmpty() || !m_net)
        return;
    m_net->sendDm(m_friendId, text);
    StickerPicker::appendMessage(
        m_view, QStringLiteral("<b style='color:#27ae60'>我：</b>"), text);
    saveMessage(true, QStringLiteral("我"), text);
    m_input->clear();
}

void ChatDialog::receiveMessage(const QString &fromName, const QString &text)
{
    StickerPicker::appendMessage(
        m_view,
        QStringLiteral("<b style='color:#c0392b'>%1：</b>").arg(fromName.toHtmlEscaped()),
        text);
    saveMessage(false, fromName, text);
}

// 打开窗口时回放历史（QSettings 里的 JSON 数组，上限 200 条）
void ChatDialog::loadHistory()
{
    QSettings st;
    const QJsonArray arr = QJsonDocument::fromJson(
        st.value(dmHistKey(m_myId, m_friendId)).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString text = o.value(QStringLiteral("t")).toString();
        if (o.value(QStringLiteral("me")).toBool()) {
            StickerPicker::appendMessage(
                m_view, QStringLiteral("<b style='color:#27ae60'>我：</b>"), text);
        } else {
            StickerPicker::appendMessage(
                m_view,
                QStringLiteral("<b style='color:#c0392b'>%1：</b>")
                    .arg(o.value(QStringLiteral("n")).toString().toHtmlEscaped()),
                text);
        }
    }
}

// 追加一条到本地历史（超过 200 条丢最旧的）
void ChatDialog::saveMessage(bool mine, const QString &name, const QString &text)
{
    QSettings st;
    const QString key = dmHistKey(m_myId, m_friendId);
    QJsonArray arr = QJsonDocument::fromJson(st.value(key).toByteArray()).array();
    QJsonObject o;
    o[QStringLiteral("me")] = mine;
    o[QStringLiteral("n")] = name;
    o[QStringLiteral("t")] = text;
    arr.append(o);
    while (arr.size() > DM_HISTORY_MAX)
        arr.removeAt(0);
    st.setValue(key, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
