#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QString>
#include <QHash>
#include <QPair>
#include <QHostAddress>

// 局域网联机的网络层：
// - 房主：startHosting() 在“房间号对应的TCP端口”监听，同时用 UDP 周期性广播房间信息
// - 加入者：startDiscovery() 监听 UDP 广播，自动发现局域网内的房间；再 connectToHost()
// 这样加入者只需要输入/选择 4 位房间号，不用记 IP。
//
// TCP 消息协议：每条以换行符结尾
//   MOVE x1,y1,x2,y2     走一步棋
//   CHAT 内容             聊天
//   AVTR 关键字           奶娃状态同步（房主->加入者：show/ready/idle/think/action/action2/...）
//   VER 版本号            连接后互报版本，不一致时提示双方更新（防止新旧混用走棋不同步）
//   UNDO REQ / UNDO YES / UNDO NO   悔棋请求与应答
//   AGAIN REQ / AGAIN YES / AGAIN NO  重新开始请求与应答
//
// UDP 房间发现（端口 kDiscoveryPort）：
//   加入者广播：CANDY1 FIND
//   房主回应/周期广播：CANDY1 HELLO 房间号 TCP端口
class NetworkManager : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 kDiscoveryPort = 45455;

    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();

    // 房主：TCP 监听 + UDP 广播房间号
    void startHosting(quint16 tcpPort, const QString &roomCode);
    // 加入者：开始扫描局域网内的房间
    void startDiscovery();
    void stopDiscovery();
    // 加入者：连接到指定房主
    void connectToHost(const QString &host, quint16 port);

    // 发送棋步 / 聊天 / 奶娃状态
    void sendMove(int x1, int y1, int x2, int y2);
    void sendChat(const QString &text);
    void sendAvatar(const QString &key);
    void sendVersion(const QString &ver);
    void sendUndoRequest();
    void sendUndoAnswer(bool accepted);
    void sendRestartRequest();
    void sendRestartAnswer(bool accepted);

    bool isConnected() const { return m_socket && m_socket->state() == QAbstractSocket::ConnectedState; }

    // 房间号 -> (房主IP, TCP端口)，扫描期间持续更新
    struct RoomInfo { QHostAddress ip; quint16 port; };
    QHash<QString, RoomInfo> rooms() const { return m_rooms; }

signals:
    void moveReceived(int x1, int y1, int x2, int y2);   // 收到对方走子
    void chatReceived(const QString &text);              // 收到对方聊天
    void avatarReceived(const QString &key);             // 收到对方奶娃状态
    void versionReceived(const QString &ver);            // 收到对方版本号
    void undoRequested();                                // 对方请求悔棋
    void undoAnswered(bool accepted);                    // 对方应答悔棋
    void restartRequested();                             // 对方请求重新开始
    void restartAnswered(bool accepted);                 // 对方应答重新开始
    void badMoveReceived();                              // 收到越界/非法走子数据（对方版本过旧？）
    void connected();        // 双方连接成功
    void disconnected();     // 对方断开连接
    void errorOccurred(const QString &msg);
    void roomFound(const QString &roomCode);              // 发现/刷新一个房间
    void roomsCleared();                                   // 扫描列表重置

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void onUdpReadyRead();
    void broadcastHello();

private:
    QTcpServer *m_server = nullptr;
    QTcpSocket *m_socket = nullptr;
    QUdpSocket *m_udp = nullptr;
    QTimer *m_beaconTimer = nullptr;
    QByteArray m_buffer;   // TCP 粘包处理：按行分割

    QString m_roomCode;           // 房主的房间号
    quint16 m_tcpPort = 0;        // 房主的 TCP 端口
    QHash<QString, RoomInfo> m_rooms;  // 加入者扫描到的房间表

    void processBuffer();
    void sendLine(const QString &line);
};

#endif // NETWORKMANAGER_H
