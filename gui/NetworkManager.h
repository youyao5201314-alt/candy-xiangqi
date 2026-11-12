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

class InternetClient;

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
//   SIDE R / SIDE B      房主通知加入者执方（随机红黑方）
//   PROFILE <base64json> 双方互发名片（昵称/头衔/头像URL/自己是否红方），观战者也收得到
//   SPECJOIN              加入者声明自己是观战者（首条消息）
//   SPEC                  房主通知该连接为观战身份
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

    // 传输方式：局域网 TCP 或互联网 WebSocket（阶段 3）
    enum class Transport { Lan, Internet };

    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();

    // 互联网模式：挂接已登录的 InternetClient，之后所有 sendXxx 走 WebSocket，
    // InternetClient 的对局信号桥接到本类同名信号，MainWindow 一行不用改
    void attachInternet(InternetClient *ic);
    Transport transport() const { return m_transport; }

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
    void sendSide(bool hostIsRed);   // 房主通知加入者执方（随机红黑）
    // 发送本机玩家名片（局域网用；互联网模式为空操作，对手信息走 match_ready）
    void sendProfile(const QString &name, const QString &title,
                     const QString &avatarUrl, bool selfRed);
    void sendSpectatorJoin();        // 加入者声明观战身份（连接后首条消息）
    void sendUndoRequest();
    void sendUndoAnswer(bool accepted);
    void sendRestartRequest();
    void sendRestartAnswer(bool accepted);
    void sendResign();      // 投降（LAN/Internet 均有效）
    void sendGameEnd(const QString &result);  // 终局上报（仅互联网落库，LAN 空操作）
    void leaveRoom();       // 离开对局房间（仅互联网，LAN 空操作）

    bool isConnected() const;
    bool isSpectator() const { return m_isSpectator; }
    void setSpectator(bool v) { m_isSpectator = v; }

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
    void resignReceived();                               // 对方投降（我方获胜）
    void badMoveReceived();                              // 收到越界/非法走子数据（对方版本过旧？）
    void sideReceived(bool hostIsRed);                   // 收到房主执方信息（加入者用）
    void spectatorReceived();                            // 收到观战身份（加入者用）
    // 收到对方名片：selfRed=发送方是否执红（观战者据此摆放双方名片）
    void profileReceived(const QString &name, const QString &title,
                         const QString &avatarUrl, bool senderRed);
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
    void onSpectatorReadyRead();   // 观战者发来消息（聊天等）
    void onSpectatorDisconnected(); // 观战者断开

private:
    QTcpServer *m_server = nullptr;
    QTcpSocket *m_socket = nullptr;
    QList<QTcpSocket*> m_spectators;  // 观战者 socket 列表（房主端维护）
    QUdpSocket *m_udp = nullptr;
    QTimer *m_beaconTimer = nullptr;
    QByteArray m_buffer;   // TCP 粘包处理：按行分割
    QByteArray m_specBuffer;  // 观战者消息缓冲（粘包处理，房主端读取观战者聊天用）

    QString m_roomCode;           // 房主的房间号
    quint16 m_tcpPort = 0;        // 房主的 TCP 端口
    QHash<QString, RoomInfo> m_rooms;  // 加入者扫描到的房间表

    Transport m_transport = Transport::Lan;
    InternetClient *m_internet = nullptr;  // 不持有所有权（归 StartScreen）
    bool m_isSpectator = false;  // 本端是否为观战者

    void processBuffer();
    void sendLine(const QString &line);
    void forwardToSpectators(const QString &line);  // 房主：转发消息给所有观战者
};

#endif // NETWORKMANAGER_H
