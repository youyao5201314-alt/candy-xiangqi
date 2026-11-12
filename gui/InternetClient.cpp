// WebSocket 实时通道实现
#include "InternetClient.h"
#include "ServerConfig.h"

#include <QWebSocket>
#include <QDebug>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

InternetClient::InternetClient(QObject *parent)
    : QObject(parent)
{
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &InternetClient::onTextMessage);
    connect(m_ws, &QWebSocket::connected,
            this, &InternetClient::connectedToServer);
    connect(m_ws, &QWebSocket::disconnected,
            this, &InternetClient::socketClosed);
}

InternetClient::~InternetClient()
{
    if (m_ws)
        m_ws->close();
}

void InternetClient::open(const QString &accessToken)
{
    m_token = accessToken;
    // 已有连接/正在关闭：abort 立即复位到 Unconnected 再开新连接。
    // 用 close() 的话是异步优雅关闭，紧跟着的 open() 会静默失败
    // （登出→再登录场景 WS 就此悄悄死掉，发什么都被 sendEvent 丢掉）
    if (m_ws->state() != QAbstractSocket::UnconnectedState)
        m_ws->abort();

    QUrl url(ServerConfig::wsUrl(QStringLiteral("/ws")));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("token"), accessToken);
    url.setQuery(q);
    m_ws->open(url);
}

void InternetClient::close()
{
    m_ws->close();
}

bool InternetClient::isOpen() const
{
    return m_ws->state() == QAbstractSocket::ConnectedState;
}

// 用户可见的主动发送（邀请/应答/私信/礼物）：连接断了不能静默丢，
// 提示用户并顺手用缓存 token 重连
bool InternetClient::ensureOpenForSend(const QString &what)
{
    if (isOpen())
        return true;
    emit serverError(what, QStringLiteral("与服务器的连接已断开，正在重连，请稍后再试"));
    if (!m_token.isEmpty() && m_ws->state() == QAbstractSocket::UnconnectedState)
        open(m_token);
    return false;
}

void InternetClient::invite(int toUserId)
{
    if (!ensureOpenForSend(QStringLiteral("invite")))
        return;
    QJsonObject d;
    d["to_user_id"] = toUserId;
    sendEvent(QStringLiteral("invite"), d);
}

void InternetClient::respondInvite(int fromUserId, bool accept)
{
    if (!ensureOpenForSend(QStringLiteral("invite_response")))
        return;
    QJsonObject d;
    d["from_user_id"] = fromUserId;
    d["accept"] = accept;
    sendEvent(QStringLiteral("invite_response"), d);
}

void InternetClient::joinMatchQueue()
{
    QJsonObject d;
    d["action"] = QStringLiteral("join");
    sendEvent(QStringLiteral("match_queue"), d);
}

void InternetClient::cancelMatchQueue()
{
    QJsonObject d;
    d["action"] = QStringLiteral("cancel");
    sendEvent(QStringLiteral("match_queue"), d);
}

void InternetClient::sendGift(int toUserId, const QString &text)
{
    if (!ensureOpenForSend(QStringLiteral("gift")))
        return;
    QJsonObject d;
    d["to_user_id"] = toUserId;
    d["text"] = text;
    sendEvent(QStringLiteral("gift"), d);
}

void InternetClient::sendDm(int toUserId, const QString &text)
{
    if (!ensureOpenForSend(QStringLiteral("dm")))
        return;
    QJsonObject d;
    d["to_user_id"] = toUserId;
    d["text"] = text;
    sendEvent(QStringLiteral("dm"), d);
}

// ---------- 观战 ----------
void InternetClient::spectateJoin(const QString &roomId)
{
    if (!ensureOpenForSend(QStringLiteral("spectate_join")))
        return;
    QJsonObject d;
    d["room_id"] = roomId;
    sendEvent(QStringLiteral("spectate_join"), d);
}

void InternetClient::spectateLeave()
{
    sendEvent(QStringLiteral("spectate_leave"), QJsonObject());
}

void InternetClient::sendSpectateChat(const QString &text)
{
    if (!isOpen())
        return;
    QJsonObject d;
    d["text"] = text;
    sendEvent(QStringLiteral("spectate_chat"), d);
}

// ---------- 阶段 3：对局事件 ----------
void InternetClient::sendMove(int x1, int y1, int x2, int y2)
{
    QJsonObject d;
    d["x1"] = x1; d["y1"] = y1; d["x2"] = x2; d["y2"] = y2;
    sendEvent(QStringLiteral("move"), d);
}

void InternetClient::sendChat(const QString &text)
{
    QJsonObject d;
    d["text"] = text;
    sendEvent(QStringLiteral("chat"), d);
}

void InternetClient::sendAvatar(const QString &key)
{
    QJsonObject d;
    d["key"] = key;
    sendEvent(QStringLiteral("avatar"), d);
}

void InternetClient::sendVersion(const QString &ver)
{
    QJsonObject d;
    d["ver"] = ver;
    sendEvent(QStringLiteral("version"), d);
}

void InternetClient::sendUndoAction(const QString &action)
{
    QJsonObject d;
    d["action"] = action;
    sendEvent(QStringLiteral("undo"), d);
}

void InternetClient::sendRestartAction(const QString &action)
{
    QJsonObject d;
    d["action"] = action;
    sendEvent(QStringLiteral("restart"), d);
}

void InternetClient::sendResign()
{
    sendEvent(QStringLiteral("resign"), QJsonObject());
}

void InternetClient::sendGameEnd(const QString &result)
{
    QJsonObject d;
    d["result"] = result;
    sendEvent(QStringLiteral("game_end"), d);
}

void InternetClient::leaveRoom()
{
    sendEvent(QStringLiteral("leave_room"), QJsonObject());
}

void InternetClient::sendEvent(const QString &event, const QJsonObject &data)
{
    if (!isOpen())
        return;
    QJsonObject env;
    env["event"] = event;
    env["data"] = data;
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(env).toJson(QJsonDocument::Compact)));
}

UserInfo InternetClient::parseUser(const QJsonObject &obj)
{
    UserInfo u;
    u.id = obj.value("id").toInt();
    u.user_id = obj.value("user_id").toString();
    u.username = obj.value("username").toString();
    u.display_name = obj.value("display_name").toString();
    u.avatar_url = obj.value("avatar_url").toString();
    u.bg_url = obj.value("bg_url").toString();
    u.title = obj.value("title").toString();
    return u;
}

void InternetClient::onTextMessage(const QString &text)
{
    const QJsonObject env = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString event = env.value("event").toString();
    const QJsonObject d = env.value("data").toObject();

    if (event == QLatin1String("hello")) {
        emit helloReceived(parseUser(d.value("user").toObject()));
    } else if (event == QLatin1String("presence_snapshot")) {
        QList<int> ids;
        const QJsonArray arr = d.value("online").toArray();
        for (const QJsonValue &v : arr)
            ids.append(v.toInt());
        emit presenceSnapshot(ids);
    } else if (event == QLatin1String("presence")) {
        emit presenceChanged(d.value("user_id").toInt(),
                             d.value("online").toBool());
    } else if (event == QLatin1String("friend_request")) {
        emit friendRequestReceived(d.value("request_id").toInt(),
                                   parseUser(d.value("from").toObject()),
                                   d.value("message").toString());
    } else if (event == QLatin1String("friend_added")) {
        emit friendAdded(parseUser(d.value("user").toObject()));
    } else if (event == QLatin1String("invite")) {
        emit inviteReceived(d.value("from_user_id").toInt(),
                            d.value("from_name").toString(),
                            d.value("from_avatar").toString());
    } else if (event == QLatin1String("invite_declined")) {
        emit inviteDeclined(d.value("from_user_id").toInt());
    } else if (event == QLatin1String("gift")) {
        emit giftReceived(d.value("from_user_id").toInt(),
                          d.value("from_name").toString(),
                          d.value("text").toString());
    } else if (event == QLatin1String("dm")) {
        emit dmReceived(d.value("from_user_id").toInt(),
                        d.value("from_name").toString(),
                        d.value("from_avatar").toString(),
                        d.value("text").toString());
    } else if (event == QLatin1String("match_ready")) {
        emit matchReady(d.value("room_id").toString(),
                        d.value("side").toString(),
                        parseUser(d.value("opponent").toObject()));
    } else if (event == QLatin1String("match_closed")) {
        emit matchClosed(d.value("room_id").toString(),
                         d.value("reason").toString());
    } else if (event == QLatin1String("error")) {
        emit serverError(d.value("code").toString(),
                         d.value("message").toString());
    } else if (event == QLatin1String("move")) {
        const int x1 = d.value("x1").toInt();
        const int y1 = d.value("y1").toInt();
        const int x2 = d.value("x2").toInt();
        const int y2 = d.value("y2").toInt();
        const bool inRange =
            x1 >= 0 && x1 <= 9 && x2 >= 0 && x2 <= 9 &&
            y1 >= 0 && y1 <= 8 && y2 >= 0 && y2 <= 8;
        if (inRange)
            emit moveReceived(x1, y1, x2, y2);
        else
            emit badMoveReceived();
    } else if (event == QLatin1String("chat")) {
        emit chatReceived(d.value("text").toString());
    } else if (event == QLatin1String("avatar")) {
        emit avatarReceived(d.value("key").toString());
    } else if (event == QLatin1String("version")) {
        emit versionReceived(d.value("ver").toString());
    } else if (event == QLatin1String("undo")) {
        const QString action = d.value("action").toString();
        if (action == QLatin1String("req"))
            emit undoRequested();
        else
            emit undoAnswered(action == QLatin1String("yes"));
    } else if (event == QLatin1String("restart")) {
        const QString action = d.value("action").toString();
        if (action == QLatin1String("req"))
            emit restartRequested();
        else
            emit restartAnswered(action == QLatin1String("yes"));
    } else if (event == QLatin1String("resign")) {
        emit resignReceived();
    } else if (event == QLatin1String("disconnect")) {
        emit peerDisconnected();
    } else if (event == QLatin1String("spectate_start")) {
        emit spectateStarted(d.value("room_id").toString(),
                             parseUser(d.value("red").toObject()),
                             parseUser(d.value("black").toObject()),
                             d.value("moves").toArray());
    } else if (event == QLatin1String("spectate_end")) {
        emit spectateEnded(d.value("reason").toString());
    } else if (event == QLatin1String("spec_chat")) {
        emit specChatReceived(d.value("from_user_id").toInt(),
                              d.value("from_name").toString(),
                              d.value("from_avatar").toString(),
                              d.value("text").toString());
    } else if (event == QLatin1String("game_end")) {
        emit gameEndReceived(d.value("result").toString());
    }
}
