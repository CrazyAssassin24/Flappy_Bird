#ifndef LAN_NETWORK_H
#define LAN_NETWORK_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QByteArray>
#include <QHash>

// LAN transport for one host and up to three remote clients. Messages use
// compact JSON terminated by '\n'; TCP is a byte stream, not a message queue.
class LanNetwork : public QObject
{
    Q_OBJECT
public:
    explicit LanNetwork(QObject *parent = nullptr);
    ~LanNetwork() override;

    bool startHost(quint16 port = 45454, int maxRemotePlayers = 3);
    void joinHost(const QString &address, quint16 port = 45454);
    void disconnectFromPeer();
    bool isHost() const { return m_isHost; }
    bool isConnected() const;
    int peerCount() const;
    quint16 listeningPort() const;
    void sendMessage(const QJsonObject &message); // host broadcasts; client sends to host
    void sendMessageToPeer(int playerId, const QJsonObject &message); // host-only targeted message

signals:
    void statusChanged(const QString &status);
    void connectedToPeer();
    void disconnectedFromPeer(int playerId);
    void messageReceived(const QJsonObject &message); // host messages include _peerId

private slots:
    void acceptConnections();
    void readClientData();
    void readPeerData();
    void handleClientDisconnected();
    void handlePeerDisconnected();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    struct PeerData { int id = -1; QByteArray buffer; };
    void attachClientSocket(QTcpSocket *socket);
    void processLine(const QByteArray &line, int peerId);
    void writeMessage(QTcpSocket *socket, const QJsonObject &message);

    QTcpServer m_server;
    QTcpSocket *m_clientSocket = nullptr;
    QHash<QTcpSocket *, PeerData> m_peers;
    int m_maxPeers = 3;
    QByteArray m_clientBuffer;
    bool m_isHost = false;
};

#endif // LAN_NETWORK_H
