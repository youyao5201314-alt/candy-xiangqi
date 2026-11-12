#include "NetworkManager.h"
#include "InternetClient.h"
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
{
}

void NetworkManager::attachInternet(InternetClient *ic)
{
    m_internet = ic;
    m_transport = Transport::Internet;

    // InternetClient 对局信号 -> 本类同名信号（信号连信号，MainWindow 无感知）
    connect(ic, &InternetClient::moveReceived, this, &NetworkManager::moveReceived);
    connect(ic, &InternetClient::chatReceived, this, &NetworkManager::chatReceived);
    connect(ic, &InternetClient::avatarReceived, this, &NetworkManager::avatarReceived);
    connect(ic, &InternetClient::versionReceived, this, &NetworkManager::versionReceived);
    connect(ic, &InternetClient::undoRequested, this, &NetworkManager::undoRequested);
    connect(ic, &InternetClient::undoAnswered, this, &NetworkManager::undoAnswered);
    connect(ic, &InternetClient::restartRequested, this, &NetworkManager::restartRequested);
    connect(ic, &InternetClient::restartAnswered, this, &NetworkManager::restartAnswered);
    connect(ic, &InternetClient::resignReceived, this, &NetworkManager::resignReceived);
    connect(ic, &InternetClient::badMoveReceived, this, &NetworkManager::badMoveReceived);
    connect(ic, &InternetClient::peerDisconnected, this, &NetworkManager::disconnected);
}

NetworkManager::~NetworkManager()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
    }
    for (QTcpSocket *spec : m_spectators) {
        spec->disconnectFromHost();
        spec->deleteLater();
    }
    m_spectators.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
    }
    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
    }
}

// ============================================================================
// 房主：TCP 监听 + UDP 房间广播
// ============================================================================
void NetworkManager::startHosting(quint16 tcpPort, const QString &roomCode)
{
    m_roomCode = roomCode;
    m_tcpPort = tcpPort;

    // TCP：等加入者连进来下棋
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, tcpPort)) {
        emit errorOccurred(QStringLiteral("无法创建房间（端口 %1）：%2").arg(tcpPort).arg(m_server->errorString()));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &NetworkManager::onNewConnection);

    // UDP：绑定发现端口，周期广播自己的房间号
    m_udp = new QUdpSocket(this);
    m_udp->bind(QHostAddress::Any, kDiscoveryPort,
                QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);

    m_beaconTimer = new QTimer(this);
    connect(m_beaconTimer, &QTimer::timeout, this, &NetworkManager::broadcastHello);
    m_beaconTimer->start(1500);   // 每 1.5 秒广播一次
    broadcastHello();
}

// ============================================================================
// 加入者：扫描房间
// ============================================================================
void NetworkManager::startDiscovery()
{
    m_rooms.clear();
    emit roomsCleared();

    if (!m_udp) {
        m_udp = new QUdpSocket(this);
        m_udp->bind(QHostAddress::Any, kDiscoveryPort,
                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
        connect(m_udp, &QUdpSocket::readyRead, this, &NetworkManager::onUdpReadyRead);
    }

    // 主动喊一声"有没有房间"，房主收到后会立刻单播回应（不用干等1.5秒）
    const QByteArray ping("CANDY1 FIND");
    m_udp->writeDatagram(ping, QHostAddress::Broadcast, kDiscoveryPort);
}

void NetworkManager::stopDiscovery()
{
    if (m_udp) {
        m_udp->close();
        m_udp->deleteLater();
        m_udp = nullptr;
    }
}

void NetworkManager::connectToHost(const QString &host, quint16 port)
{
    // 连接前停掉扫描，释放 UDP 端口（房主端需要用它继续广播）
    stopDiscovery();

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
    connect(m_socket, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::errorOccurred),
            this, &NetworkManager::onError);
    connect(m_socket, &QTcpSocket::connected, this, &NetworkManager::connected);
    m_socket->connectToHost(host, port);
}

// 房主：对方连进来了
void NetworkManager::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *s = m_server->nextPendingConnection();
        // 已有对局者：新连接是观战者
        if (m_socket) {
            m_spectators.append(s);
            connect(s, &QTcpSocket::readyRead, this, &NetworkManager::onSpectatorReadyRead);
            connect(s, &QTcpSocket::disconnected, this, &NetworkManager::onSpectatorDisconnected);
            // 通知观战者身份
            s->write("SPEC\n");
            s->flush();
            continue;
        }
        // 第一个连接：对局者
        m_socket = s;
        connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
        connect(m_socket, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::errorOccurred),
                this, &NetworkManager::onError);
        emit connected();
        // 停止 UDP 广播（不再对外宣告房间），但保持 TCP 监听以接受观战者
        if (m_beaconTimer)
            m_beaconTimer->stop();
    }
}

// ============================================================================
// UDP：处理房间发现数据报
// ============================================================================
void NetworkManager::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_udp->receiveDatagram();
        const QString text = QString::fromUtf8(dg.data()).trimmed();
        const QStringList parts = text.split(' ');
        if (parts.size() < 2 || parts[0] != QStringLiteral("CANDY1"))
            continue;

        if (parts[1] == QStringLiteral("FIND")) {
            // 加入者在找房间 → 房主单播回应一次
            if (!m_roomCode.isEmpty()) {
                const QByteArray hello =
                    QStringLiteral("CANDY1 HELLO %1 %2").arg(m_roomCode).arg(m_tcpPort).toUtf8();
                m_udp->writeDatagram(hello, dg.senderAddress(), dg.senderPort());
            }
        } else if (parts[1] == QStringLiteral("HELLO") && parts.size() >= 4) {
            // 房主宣告房间 → 加入者记入房间表
            const QString code = parts[2];
            bool ok = false;
            const quint16 port = parts[3].toUShort(&ok);
            if (!ok)
                continue;
            const bool isNew = !m_rooms.contains(code);
            m_rooms.insert(code, { dg.senderAddress(), port });
            if (isNew)
                emit roomFound(code);
        }
    }
}

void NetworkManager::broadcastHello()
{
    if (!m_udp || m_roomCode.isEmpty())
        return;
    const QByteArray hello =
        QStringLiteral("CANDY1 HELLO %1 %2").arg(m_roomCode).arg(m_tcpPort).toUtf8();
    m_udp->writeDatagram(hello, QHostAddress::Broadcast, kDiscoveryPort);
}

// ============================================================================
// TCP：棋步 / 聊天
// ============================================================================
void NetworkManager::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void NetworkManager::processBuffer()
{
    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);
        if (line.isEmpty())
            continue;

        if (line.startsWith("SIDE ")) {
            // 房主通知执方：SIDE R（房主红）或 SIDE B（房主黑）
            emit sideReceived(line.mid(5).trimmed() == "R");
        } else if (line == "SPEC") {
            // 房主通知：本连接为观战身份
            m_isSpectator = true;
            emit spectatorReceived();
        } else if (line.startsWith("MOVE ")) {
            // 房主端：转发给观战者
            if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty())
                forwardToSpectators(QString::fromUtf8(line));
            QList<QByteArray> parts = line.mid(5).split(',');
            if (parts.size() == 4) {
                bool ok = true;
                int v[4];
                for (int i = 0; i < 4; i++) {
                    bool o = false;
                    v[i] = parts[i].toInt(&o);
                    ok = ok && o;
                }
                // 越界坐标（旧版 bug 会发 -1,-1）：不执行，通知界面提示
                const bool inRange = ok
                    && v[0] >= 0 && v[0] <= 9 && v[1] >= 0 && v[1] <= 8
                    && v[2] >= 0 && v[2] <= 9 && v[3] >= 0 && v[3] <= 8;
                if (inRange)
                    emit moveReceived(v[0], v[1], v[2], v[3]);
                else
                    emit badMoveReceived();
            }
        } else if (line.startsWith("CHAT ")) {
            // 房主端：转发给观战者
            if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty())
                forwardToSpectators(QString::fromUtf8(line));
            emit chatReceived(QString::fromUtf8(line.mid(5)));
        } else if (line.startsWith("AVTR ")) {
            if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty())
                forwardToSpectators(QString::fromUtf8(line));
            emit avatarReceived(QString::fromUtf8(line.mid(5)));
        } else if (line.startsWith("PROFILE ")) {
            // 对方名片：房主端转发给观战者（让观战双方名片都能显示）
            if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty())
                forwardToSpectators(QString::fromUtf8(line));
            // base64 JSON: {"n":昵称,"t":头衔,"a":头像URL,"r":是否红方}
            const QJsonObject po = QJsonDocument::fromJson(
                QByteArray::fromBase64(line.mid(8))).object();
            emit profileReceived(po.value("n").toString(),
                                 po.value("t").toString(),
                                 po.value("a").toString(),
                                 po.value("r").toBool());
        } else if (line.startsWith("VER ")) {
            emit versionReceived(QString::fromUtf8(line.mid(4)));
        } else if (line == "UNDO REQ") {
            emit undoRequested();
        } else if (line == "UNDO YES") {
            emit undoAnswered(true);
        } else if (line == "UNDO NO") {
            emit undoAnswered(false);
        } else if (line == "AGAIN REQ") {
            emit restartRequested();
        } else if (line == "AGAIN YES") {
            emit restartAnswered(true);
        } else if (line == "AGAIN NO") {
            emit restartAnswered(false);
        } else if (line == "RESIGN") {
            if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty())
                forwardToSpectators(QString::fromUtf8(line));
            emit resignReceived();
        }
    }
}

void NetworkManager::sendMove(int x1, int y1, int x2, int y2)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendMove(x1, y1, x2, y2);
        return;
    }
    sendLine(QStringLiteral("MOVE %1,%2,%3,%4").arg(x1).arg(y1).arg(x2).arg(y2));
}

void NetworkManager::sendChat(const QString &text)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendChat(text);
        return;
    }
    sendLine(QStringLiteral("CHAT ") + text);
}

void NetworkManager::sendAvatar(const QString &key)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendAvatar(key);
        return;
    }
    sendLine(QStringLiteral("AVTR ") + key);
}

void NetworkManager::sendVersion(const QString &ver)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendVersion(ver);
        return;
    }
    sendLine(QStringLiteral("VER ") + ver);
}

void NetworkManager::sendSide(bool hostIsRed)
{
    sendLine(hostIsRed ? QStringLiteral("SIDE R") : QStringLiteral("SIDE B"));
}

void NetworkManager::sendProfile(const QString &name, const QString &title,
                                 const QString &avatarUrl, bool selfRed)
{
    // 互联网模式对手信息由 match_ready 下发，不需要 LAN 名片
    if (m_transport == Transport::Internet)
        return;
    QJsonObject p;
    p["n"] = name;
    p["t"] = title;
    p["a"] = avatarUrl;
    p["r"] = selfRed;
    const QByteArray b64 =
        QJsonDocument(p).toJson(QJsonDocument::Compact).toBase64();
    sendLine(QStringLiteral("PROFILE ") + QString::fromLatin1(b64));
}

void NetworkManager::sendSpectatorJoin()
{
    // 观战者连接后不需要发任何特殊消息：房主端检测到 m_socket 已存在即判定为观战者
    // 此方法保留为空，协议层面观战者不发 SPECJOIN
}

void NetworkManager::sendUndoRequest()
{
    if (m_transport == Transport::Internet) {
        m_internet->sendUndoAction(QStringLiteral("req"));
        return;
    }
    sendLine(QStringLiteral("UNDO REQ"));
}

void NetworkManager::sendUndoAnswer(bool accepted)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendUndoAction(accepted ? QStringLiteral("yes") : QStringLiteral("no"));
        return;
    }
    sendLine(accepted ? QStringLiteral("UNDO YES") : QStringLiteral("UNDO NO"));
}

void NetworkManager::sendRestartRequest()
{
    if (m_transport == Transport::Internet) {
        m_internet->sendRestartAction(QStringLiteral("req"));
        return;
    }
    sendLine(QStringLiteral("AGAIN REQ"));
}

void NetworkManager::sendRestartAnswer(bool accepted)
{
    if (m_transport == Transport::Internet) {
        m_internet->sendRestartAction(accepted ? QStringLiteral("yes") : QStringLiteral("no"));
        return;
    }
    sendLine(accepted ? QStringLiteral("AGAIN YES") : QStringLiteral("AGAIN NO"));
}

void NetworkManager::sendResign()
{
    if (m_transport == Transport::Internet) {
        m_internet->sendResign();
        return;
    }
    sendLine(QStringLiteral("RESIGN"));
}

void NetworkManager::sendGameEnd(const QString &result)
{
    // 仅互联网模式需要服务器落库
    if (m_transport == Transport::Internet)
        m_internet->sendGameEnd(result);
}

void NetworkManager::leaveRoom()
{
    if (m_transport == Transport::Internet)
        m_internet->leaveRoom();
}

void NetworkManager::sendLine(const QString &line)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->write((line + "\n").toUtf8());
    m_socket->flush();
    // 房主端：同时转发给所有观战者（SIDE/SPEC 等握手消息不转发）
    if (m_transport == Transport::Lan && m_server && !m_spectators.isEmpty()
        && !line.startsWith("SIDE ") && line != "SPEC")
        forwardToSpectators(line);
}

void NetworkManager::forwardToSpectators(const QString &line)
{
    const QByteArray data = (line + "\n").toUtf8();
    for (QTcpSocket *spec : m_spectators) {
        if (spec->state() == QAbstractSocket::ConnectedState) {
            spec->write(data);
            spec->flush();
        }
    }
}

// 房主端：收到观战者发来的消息（仅聊天）
void NetworkManager::onSpectatorReadyRead()
{
    QTcpSocket *spec = qobject_cast<QTcpSocket*>(sender());
    if (!spec) return;
    while (spec->canReadLine()) {
        QByteArray line = spec->readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("CHAT ")) {
            QString text = QString::fromUtf8(line);
            // 转发给对局者
            if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
                m_socket->write((text + "\n").toUtf8());
                m_socket->flush();
            }
            // 转发给其他观战者
            for (QTcpSocket *other : m_spectators) {
                if (other != spec && other->state() == QAbstractSocket::ConnectedState) {
                    other->write((text + "\n").toUtf8());
                    other->flush();
                }
            }
            // 房主本地显示
            emit chatReceived(QStringLiteral("【观战】") + QString::fromUtf8(line.mid(5)));
        }
    }
}

void NetworkManager::onSpectatorDisconnected()
{
    QTcpSocket *spec = qobject_cast<QTcpSocket*>(sender());
    if (!spec) return;
    m_spectators.removeOne(spec);
    spec->deleteLater();
}

bool NetworkManager::isConnected() const
{
    if (m_transport == Transport::Internet)
        return m_internet && m_internet->isOpen();
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NetworkManager::onDisconnected()
{
    emit disconnected();
}

void NetworkManager::onError(QAbstractSocket::SocketError err)
{
    // 对方正常退出（优雅断连）由 disconnected() 处理，不算错误，避免重复弹窗
    if (err == QAbstractSocket::RemoteHostClosedError)
        return;
    if (m_socket)
        emit errorOccurred(m_socket->errorString());
}
