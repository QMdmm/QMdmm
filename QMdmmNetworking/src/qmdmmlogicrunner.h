// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMLOGICRUNNER_H
#define QMDMMLOGICRUNNER_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmLogicConfiguration>
#include <QMdmmProtocol>

QMDMM_EXPORT_NAME(QMdmmLogicRunner)

namespace QMdmmNetworking {

#ifndef DOXYGEN
namespace p {
class LogicRunnerP;
}
#endif

#ifndef DOXYGEN
namespace v0 {
#endif

class Agent;

// TODO: Implement Lobby when other contents are ready
class QMDMMNETWORKING_EXPORT LogicRunner final : public QObject
{
    Q_OBJECT

public:
    Q_DISABLE_COPY_MOVE(LogicRunner);

    explicit LogicRunner(const QMdmmCore::LogicConfiguration &logicConfiguration, int playerNumPerRoom, QObject *parent = nullptr);
    ~LogicRunner() override;

    Agent *addAgent(Agent *agent);
    Agent *reconnectAgent(Agent *agent);

    Agent *agent(const QString &playerName);
    [[nodiscard]] const Agent *agent(const QString &playerName) const;

    [[nodiscard]] bool full() const;

signals: // NOLINT(readability-redundant-access-specifiers)
    void gameOver(QPrivateSignal);

#ifndef DOXYGEN
private:
    friend class p::LogicRunnerP;
    // LogicRunnerP is QObject. QPointer can't be used since it is incomplete here
    p::LogicRunnerP *const d;
#endif
};

#ifndef DOXYGEN
} // namespace v0

inline namespace v1 {
using v0::LogicRunner;
}
#endif

} // namespace QMdmmNetworking
#endif // QMDMMLOGICRUNNER_H
