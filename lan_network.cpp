#include "lan_network.h"

#include <QJsonDocument>
#include <QHostAddress>
#include <QTimer>

namespace { constexpr int MaxPeers = 3; constexpr int MaxMessageBytes = 1024 * 1024; }

LanNetwork::LanNetwork(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &LanNetwork::acceptConnections);
}

LanNetwork::~LanNetwork()
{
    disconnectFromPeer();
    m_server.close();
}

bool LanNetwork::startHost(quint16 port, int maxRemotePlayers)
{
    disconnectFromPeer();
    m_server.close();
    m_isHost = true;
    m_maxPeers = qBound(1, maxRemotePlayers, MaxPeers);
    if (!m_server.listen(QHostAddress::AnyIPv4, port)) {
        emit statusChanged(QStringLiteral("Could not host: %1").arg(m_server.errorString()));
        return false;
    }
    emit statusChanged(QStringLiteral("Hosting LAN game on port %1 — waiting for players…").arg(m_server.serverPort()));
    return true;
}

void LanNetwork::joinHost(const QString &address, quint16 port)
{
    disconnectFromPeer();
    m_server.close();
    m_isHost = false;
    auto *socket = new QTcpSocket(this);
    m_clientSocket = socket;
    connect(socket, &QTcpSocket::connected, this, [this]() {
        emit statusChanged(QStringLiteral("Connected to host; waiting for match settings…"));
        emit connectedToPeer();
        writeMessage(m_clientSocket, QJsonObject{{QStringLiteral("type"), QStringLiteral("hello")},
                                                {QStringLiteral("protocol"), 2}});
    });
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::readyRead, this, &LanNetwork::readClientData);
    connect(socket, &QTcpSocket::disconnected, this, &LanNetwork::handleClientDisconnected);
    connect(socket, &QTcpSocket::errorOccurred, this, &LanNetwork::handleSocketError);
    emit statusChanged(QStringLiteral("Connecting to %1:%2…").arg(address).arg(port));
    socket->connectToHost(address, port);
}

void LanNetwork::disconnectFromPeer()
{
    if (m_clientSocket) {
        QTcpSocket *old = m_clientSocket;
        m_clientSocket = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    const auto sockets = m_peers.keys();
    m_peers.clear();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    m_clientBuffer.clear();
}

bool LanNetwork::isConnected() const
{
    if (m_isHost) return !m_peers.isEmpty();
    return m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState;
}

int LanNetwork::peerCount() const
{
    return m_isHost ? m_peers.size() : (isConnected() ? 1 : 0);
}

quint16 LanNetwork::listeningPort() const
{
    return m_server.isListening() ? m_server.serverPort() : 0;
}

void LanNetwork::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (m_peers.size() >= m_maxPeers) {
            writeMessage(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("reject")},
                                             {QStringLiteral("reason"), QStringLiteral("Room is full (maximum 4 players).")}});
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        int id = 1;
        while (id <= MaxPeers) {
            bool used = false;
            for (const PeerData &existing : m_peers) if (existing.id == id) { used = true; break; }
            if (!used) break;
            ++id;
        }
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_peers.insert(socket, PeerData{id, {}});
        connect(socket, &QTcpSocket::readyRead, this, &LanNetwork::readPeerData);
        connect(socket, &QTcpSocket::disconnected, this, &LanNetwork::handlePeerDisconnected);
        connect(socket, &QTcpSocket::errorOccurred, this, &LanNetwork::handleSocketError);
        writeMessage(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("welcome")},
                                         {QStringLiteral("playerId"), id},
                                         {QStringLiteral("protocol"), 2}});
        emit statusChanged(QStringLiteral("Player %1 connected (%2/4 total).")
                           .arg(id + 1).arg(m_peers.size() + 1));
        emit connectedToPeer();
    }
}

void LanNetwork::readClientData()
{
    if (!m_clientSocket) return;
    m_clientBuffer.append(m_clientSocket->readAll());
    if (m_clientBuffer.size() > MaxMessageBytes) { m_clientSocket->abort(); return; }
    int newline = -1;
    while ((newline = m_clientBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_clientBuffer.left(newline).trimmed();
        m_clientBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line, 0);
    }
}

void LanNetwork::readPeerData()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket || !m_peers.contains(socket)) return;
    PeerData &peer = m_peers[socket];
    peer.buffer.append(socket->readAll());
    if (peer.buffer.size() > MaxMessageBytes) { socket->abort(); return; }
    int newline = -1;
    while ((newline = peer.buffer.indexOf('\n')) >= 0) {
        const QByteArray line = peer.buffer.left(newline).trimmed();
        peer.buffer.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line, peer.id);
    }
}

void LanNetwork::processLine(const QByteArray &line, int peerId)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        emit statusChanged(QStringLiteral("Received an invalid network message."));
        return;
    }
    QJsonObject message = doc.object();
    if (m_isHost) message.insert(QStringLiteral("_peerId"), peerId);
    const QString type = message.value(QStringLiteral("type")).toString();
    if (m_isHost && type == QLatin1String("hello")) {
        // The welcome packet already carries the authoritative player ID.
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
            if (it.value().id == peerId) {
                writeMessage(it.key(), QJsonObject{{QStringLiteral("type"), QStringLiteral("hello_ack")},
                                                   {QStringLiteral("protocol"), 2}});
                break;
            }
        }
    }
    emit messageReceived(message);
}

void LanNetwork::sendMessage(const QJsonObject &message)
{
    if (m_isHost) {
        for (QTcpSocket *socket : m_peers.keys()) writeMessage(socket, message);
    } else if (m_clientSocket && m_clientSocket->state() == QAbstractSocket::ConnectedState) {
        writeMessage(m_clientSocket, message);
    }
}

void LanNetwork::sendMessageToPeer(int playerId, const QJsonObject &message)
{
    if (!m_isHost) return;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it.value().id == playerId) {
            writeMessage(it.key(), message);
            return;
        }
    }
}

void LanNetwork::writeMessage(QTcpSocket *socket, const QJsonObject &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    // State snapshots become stale quickly. If a slow peer already has a large
    // TCP write queue, skip this snapshot instead of adding more latency. Never
    // drop control/input/handshake messages.
    if (message.value(QStringLiteral("type")).toString() == QLatin1String("state")
        && socket->bytesToWrite() > 32 * 1024)
        return;

    QByteArray packet = QJsonDocument(message).toJson(QJsonDocument::Compact);
    packet.append('\n');
    socket->write(packet);
}

void LanNetwork::handleClientDisconnected()
{
    emit statusChanged(QStringLiteral("Disconnected from LAN host."));
    if (m_clientSocket) { m_clientSocket->deleteLater(); m_clientSocket = nullptr; }
    m_clientBuffer.clear();
    emit disconnectedFromPeer(-1);
}

void LanNetwork::handlePeerDisconnected()
{
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;
    const int id = m_peers.value(socket).id;
    m_peers.remove(socket);
    socket->deleteLater();
    emit statusChanged(QStringLiteral("Player %1 disconnected.").arg(id + 1));
    emit disconnectedFromPeer(id);
}

void LanNetwork::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket) emit statusChanged(QStringLiteral("Network error: %1").arg(socket->errorString()));
}
