#ifndef INTERNETCLIENT_H
#define INTERNETCLIENT_H

#include <QObject>
#include <QString>
#include <QList>
#include "api.h"

class QWebSocket;

// 互联网实时通道：QWebSocket 长连接，JSON 文本帧 {event, data}
// 阶段 2：鉴权握手 / 在线状态 / 好友请求实时推送 / 邀请 / 匹配
// 阶段 3 会继续加 move/chat/avatar/undo/restart，信号与 NetworkManager 对齐
class InternetClient : public QObject
{
    Q_OBJECT
public:
    explicit InternetClient(QObject *parent = nullptr);
    ~InternetClient();

    // 用 access token 打开 wss/ws 连接（token 走 query 参数）
    void open(const QString &accessToken);
    void close();
    bool isOpen() const;

    // 发送邀请 / 回应邀请
    void invite(int toUserId);
    void respondInvite(int fromUserId, bool accept);
    // 随机快速匹配（互联网模式）：加入/取消队列，配对成功走 matchReady
    void joinMatchQueue();
    void cancelMatchQueue();
    // 送礼（趣味 emoji/消息，仅在线好友）
    void sendGift(int toUserId, const QString &text);
    // 好友私信（文字或奶龙表情标记，仅在线好友）
    void sendDm(int toUserId, const QString &text);

    // ---------- 阶段 3：对局事件（信号签名与 NetworkManager 对齐） ----------
    void sendMove(int x1, int y1, int x2, int y2);
    void sendChat(const QString &text);
    void sendAvatar(const QString &key);
    void sendVersion(const QString &ver);
    void sendUndoAction(const QString &action);   // "req" / "yes" / "no"
    void sendRestartAction(const QString &action);
    void sendResign();
    void sendGameEnd(const QString &result);      // "red_win" / "black_win" / "draw"
    void leaveRoom();                             // 关闭对局窗口时通知服务器

signals:
    void connectedToServer();                  // WebSocket 已连接
    void helloReceived(const UserInfo &self);  // 服务器确认身份
    void presenceSnapshot(const QList<int> &onlineUserIds);
    void presenceChanged(int userId, bool online);
    void friendRequestReceived(int requestId, const UserInfo &from, const QString &message);
    void friendAdded(const UserInfo &user);
    void inviteReceived(int fromUserId, const QString &fromName);
    void inviteDeclined(int fromUserId);
    void giftReceived(int fromUserId, const QString &fromName, const QString &text);
    void dmReceived(int fromUserId, const QString &fromName, const QString &text);
    void matchReady(const QString &roomId, const QString &side, const UserInfo &opponent);
    void matchClosed(const QString &roomId, const QString &reason);
    void serverError(const QString &code, const QString &msg);
    void socketClosed();

    // 对局事件（与 NetworkManager 同名信号一一对应，便于桥接）
    void moveReceived(int x1, int y1, int x2, int y2);
    void chatReceived(const QString &text);
    void avatarReceived(const QString &key);
    void versionReceived(const QString &ver);
    void undoRequested();
    void undoAnswered(bool accepted);
    void restartRequested();
    void restartAnswered(bool accepted);
    void resignReceived();                     // 对方投降，我方获胜
    void peerDisconnected();                   // 对方离开/掉线
    void badMoveReceived();                    // 非法走子（理论上服务器已拦）

private slots:
    void onTextMessage(const QString &text);

private:
    QWebSocket *m_ws = nullptr;
    QString m_token;

    // 用户可见发送前的连接自检：断线时提示并尝试重连（返回 false=本次没发出去）
    bool ensureOpenForSend(const QString &what);
    void sendEvent(const QString &event, const class QJsonObject &data);
    static UserInfo parseUser(const class QJsonObject &obj);
};

#endif // INTERNETCLIENT_H
