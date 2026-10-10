// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMCLIENT_H
#define QMDMMCLIENT_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmRoom>

#include <QMap>
#include <QObject>

QMDMM_EXPORT_NAME(QMdmmClientConfiguration)
QMDMM_EXPORT_NAME(QMdmmClient)

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace p {
class ClientP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class Agent;

struct QMDMMNETWORKING_EXPORT ClientConfiguration final : public QVariantMap
{
    Q_GADGET
    Q_PROPERTY(QString screenName READ screenName WRITE setScreenName DESIGNABLE false FINAL)

public:
    // The enclosing class carries QMDMMNETWORKING_EXPORT, which is what exports this one; MSVC rejects the second marking (C2487).
    static const ClientConfiguration &defaults();

#ifdef Q_MOC_RUN
    Q_INVOKABLE QMdmmClientConfiguration();
    Q_INVOKABLE QMdmmClientConfiguration(const QMdmmClientConfiguration &);
#else
    using QVariantMap::QMap;
#endif

    [[nodiscard]] QString screenName() const;
    void setScreenName(const QString &screenName);

#ifndef DOXYGEN
private:
    using QVariantMap::operator=;
#endif
};

class QMDMMNETWORKING_EXPORT Client final : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(Client);

    explicit Client(ClientConfiguration clientConfiguration, QObject *parent = nullptr);
    ~Client() override;

    bool connectToHost(const QString &host, QMdmmCore::Data::AgentState initialState);

    void disconnectFromHost();
    [[nodiscard]] bool isConnected() const;

    [[nodiscard]] QMdmmCore::Room *room();
    [[nodiscard]] const QMdmmCore::Room *room() const;

    [[nodiscard]] Agent *agent();
    [[nodiscard]] const Agent *agent() const;

signals:
    void socketConnectionLost(const QString &errorString, QPrivateSignal);

    void socketReconnectSucceeded(QPrivateSignal);

    void socketErrorDisconnected(const QString &errorString, QPrivateSignal);

#ifndef DOXYGEN
private:
    friend class p::ClientP;
    // ClientP is QObject. QPointer can't be used since it is incomplete here
    p::ClientP *const d;
#endif
};

#ifndef DOXYGEN
} // namespace v0

inline namespace v1 {
using v0::Client;
using v0::ClientConfiguration;
} // namespace v1
#endif
} // namespace QMdmmNetworking

#endif
