#include "NetworkManager.h"
#include <QNetworkDatagram>
#include <QRandomGenerator>

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
{
}

NetworkManager::~NetworkManager()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
    }
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
        // 已有对手：直接拒绝多余连接（防止对局中被第三者顶掉棋盘通道）
        if (m_socket) {
            s->abort();
            s->deleteLater();
            continue;
        }
        m_socket = s;
        connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
        connect(m_socket, &QTcpSocket::disconnected, this, &NetworkManager::onDisconnected);
        connect(m_socket, qOverload<QAbstractSocket::SocketError>(&QAbstractSocket::errorOccurred),
                this, &NetworkManager::onError);
        emit connected();
    }
    // 只支持一房一人：有人进房后关闭监听并停止广播
    if (m_socket && m_server->isListening())
        m_server->close();
    if (m_beaconTimer)
        m_beaconTimer->stop();
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

        if (line.startsWith("MOVE ")) {
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
            emit chatReceived(QString::fromUtf8(line.mid(5)));
        } else if (line.startsWith("AVTR ")) {
            emit avatarReceived(QString::fromUtf8(line.mid(5)));
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
        }
    }
}

void NetworkManager::sendMove(int x1, int y1, int x2, int y2)
{
    sendLine(QStringLiteral("MOVE %1,%2,%3,%4").arg(x1).arg(y1).arg(x2).arg(y2));
}

void NetworkManager::sendChat(const QString &text)
{
    sendLine(QStringLiteral("CHAT ") + text);
}

void NetworkManager::sendAvatar(const QString &key)
{
    sendLine(QStringLiteral("AVTR ") + key);
}

void NetworkManager::sendVersion(const QString &ver)
{
    sendLine(QStringLiteral("VER ") + ver);
}

void NetworkManager::sendUndoRequest()
{
    sendLine(QStringLiteral("UNDO REQ"));
}

void NetworkManager::sendUndoAnswer(bool accepted)
{
    sendLine(accepted ? QStringLiteral("UNDO YES") : QStringLiteral("UNDO NO"));
}

void NetworkManager::sendRestartRequest()
{
    sendLine(QStringLiteral("AGAIN REQ"));
}

void NetworkManager::sendRestartAnswer(bool accepted)
{
    sendLine(accepted ? QStringLiteral("AGAIN YES") : QStringLiteral("AGAIN NO"));
}

void NetworkManager::sendLine(const QString &line)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->write((line + "\n").toUtf8());
    m_socket->flush();
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
