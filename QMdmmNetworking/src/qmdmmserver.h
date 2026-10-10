// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMSERVER_H
#define QMDMMSERVER_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmProtocol>
#include <QMdmmRoom>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>

QMDMM_EXPORT_NAME(QMdmmServerConfiguration)
QMDMM_EXPORT_NAME(QMdmmServer)

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace p {
class ServerP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

struct QMDMMNETWORKING_EXPORT ServerConfiguration final : public QJsonObject
{
    Q_GADGET
    Q_PROPERTY(bool tcpEnabled READ tcpEnabled WRITE setTcpEnabled DESIGNABLE false FINAL)
    Q_PROPERTY(uint16_t tcpPort READ tcpPort WRITE setTcpPort DESIGNABLE false FINAL)
    Q_PROPERTY(bool localEnabled READ localEnabled WRITE setLocalEnabled DESIGNABLE false FINAL)
    Q_PROPERTY(QString localSocketName READ localSocketName WRITE setLocalSocketName DESIGNABLE false FINAL)
    Q_PROPERTY(bool websocketEnabled READ websocketEnabled WRITE setWebsocketEnabled DESIGNABLE false FINAL)
    Q_PROPERTY(QString websocketName READ websocketName WRITE setWebsocketName DESIGNABLE false FINAL)
    Q_PROPERTY(uint16_t websocketPort READ websocketPort WRITE setWebsocketPort DESIGNABLE false FINAL)
    Q_PROPERTY(int playerNumPerRoom READ playerNumPerRoom WRITE setPlayerNumPerRoom DESIGNABLE false FINAL)
    Q_PROPERTY(int requestTimeout READ requestTimeout WRITE setRequestTimeout DESIGNABLE false FINAL)

public:
    // The enclosing class carries QMDMMNETWORKING_EXPORT, which is what exports this one; MSVC rejects the second marking (C2487).
    static const ServerConfiguration &defaults();

#ifdef Q_MOC_RUN
    Q_INVOKABLE QMdmmServerConfiguration();
    Q_INVOKABLE QMdmmServerConfiguration(const QMdmmServerConfiguration &);
#else
    using QJsonObject::QJsonObject;
#endif

    [[nodiscard]] bool tcpEnabled() const;
    void setTcpEnabled(bool tcpEnabled);
    [[nodiscard]] uint16_t tcpPort() const;
    void setTcpPort(uint16_t tcpPort);
    [[nodiscard]] bool localEnabled() const;
    void setLocalEnabled(bool localEnabled);
    [[nodiscard]] QString localSocketName() const;
    void setLocalSocketName(const QString &localSocketName);
    [[nodiscard]] bool websocketEnabled() const;
    void setWebsocketEnabled(bool websocketEnabled);
    [[nodiscard]] QString websocketName() const;
    void setWebsocketName(const QString &websocketName);
    [[nodiscard]] uint16_t websocketPort() const;
    void setWebsocketPort(uint16_t websocketPort);
    [[nodiscard]] int playerNumPerRoom() const;
    void setPlayerNumPerRoom(int playerNumPerRoom);
    [[nodiscard]] int requestTimeout() const;
    void setRequestTimeout(int requestTimeout);

    bool deserialize(const QJsonValue &value);

#ifndef DOXYGEN
private:
    using QJsonObject::operator=;
#endif
};

class QMDMMNETWORKING_EXPORT Server : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(Server);

    explicit Server(ServerConfiguration serverConfiguration, QMdmmCore::LogicConfiguration logicConfiguration, QObject *parent = nullptr);
    ~Server() override;

public slots: // NOLINT(readability-redundant-access-specifiers)
    [[nodiscard]] bool listen();
    void close();

signals:
    void listenError(const QString &transportName, const QString &errorString, QPrivateSignal);

#ifndef DOXYGEN
private:
    // ServerP is QObject. QPointer can't be used since it is incomplete here
    p::ServerP *const d;
#endif
};

#ifndef DOXYGEN
} // namespace v0

inline namespace v1 {
using v0::Server;
using v0::ServerConfiguration;
} // namespace v1
#endif

} // namespace QMdmmNetworking

#endif // QMDMMSERVER_H
