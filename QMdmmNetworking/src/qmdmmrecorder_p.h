// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMNETWORKING_QMDMMRECORDER_P_H
#define QMDMMNETWORKING_QMDMMRECORDER_P_H

#include "qmdmmnetworkingglobal.h"

#include <QMdmmProtocol>

#include <QByteArray>
#include <QFile>
#include <QString>

namespace QMdmmNetworking {
namespace p {

// One game, one file. The record is JSON lines: the first line says what the run was (build
// version plus the configuration the game was played under), and every following line is one
// packet of the room's broadcast view -- the roster it hands a joining client, the game and round
// starts, the four round events and the round and game overs -- with a strictly increasing
// sequence number. A chat line or an operate notice is not part of a game and is not recorded.
//
// The file is opened when the game starts and closed when it is over, so a room that never fills
// leaves nothing behind. A directory that cannot be written disables recording after one warning:
// a game must not fail because its transcript could not be written, and a silently partial file
// would be worse than none.
class QMDMMNETWORKING_PRIVATE_EXPORT RecorderP final
{
    Q_DISABLE_COPY_MOVE(RecorderP)

public:
    RecorderP() = default;
    ~RecorderP();

    // The directory to record into and the header line, both supplied before the game starts.
    // An empty directory means recording is off, which is the default.
    void setTarget(const QString &directory, const QByteArray &header);

    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isOpen() const;

    void open();
    void record(const QMdmmCore::Packet &packet);
    void close();

private:
    void disable(const QString &reason);

    QString directory;
    QByteArray header;
    QFile file;
    qint64 sequence = 0;
    bool failed = false;
};

} // namespace p
} // namespace QMdmmNetworking

#endif
