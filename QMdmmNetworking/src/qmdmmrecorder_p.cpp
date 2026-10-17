// SPDX-License-Identifier: AGPL-3.0-or-later

#include "qmdmmrecorder_p.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>

using namespace Qt::StringLiterals;

namespace QMdmmNetworking {
namespace p {

RecorderP::~RecorderP()
{
    close();
}

void RecorderP::setTarget(const QString &directory, const QByteArray &header)
{
    close();

    this->directory = directory;
    this->header = header;
    this->sequence = 0;
    this->failed = false;
}

bool RecorderP::isEnabled() const
{
    return !failed && !directory.isEmpty();
}

bool RecorderP::isOpen() const
{
    return file.isOpen();
}

void RecorderP::open()
{
    if (!isEnabled() || file.isOpen())
        return;

    const QDir dir(directory);
    if (!dir.mkpath(u"."_s)) {
        disable(u"the directory cannot be created"_s);
        return;
    }

    // One file per game, named so that two games recorded into the same directory do not collide:
    // the timestamp has millisecond resolution, and the process id tells two servers apart.
    const QString name = u"game-%1-%2.jsonl"_s.arg(QDateTime::currentDateTimeUtc().toString(u"yyyyMMdd'T'HHmmsszzz"_s), QString::number(QCoreApplication::applicationPid()));
    file.setFileName(dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        disable(file.errorString());
        return;
    }

    file.write(header);
    file.write("\n");
    file.flush();
    sequence = 0;
}

void RecorderP::record(const QMdmmCore::Packet &packet)
{
    if (!file.isOpen())
        return;

    QString errorString;
    if (packet.hasError(&errorString)) {
        disable(u"the packet to record is invalid: %1"_s.arg(errorString));
        return;
    }

    ++sequence;

    // The packet is already its own JSON object, so it is embedded verbatim rather than parsed and
    // re-serialized: the line then holds exactly the bytes the clients were sent.
    QByteArray line;
    line.reserve(64);
    line.append(R"({"seq":)");
    line.append(QByteArray::number(sequence));
    line.append(R"(,"direction":"server-to-client","packet":)");
    line.append(packet.serialize());
    line.append("}\n");

    if (file.write(line) != static_cast<qint64>(line.size()))
        disable(file.errorString());
    else
        file.flush();
}

void RecorderP::close()
{
    if (!file.isOpen())
        return;

    file.flush();
    file.close();
}

void RecorderP::disable(const QString &reason)
{
    failed = true;
    qWarning() << "RecorderP: recording is off, the game record cannot be written:" << reason << "directory:" << directory;
    close();
}

} // namespace p
} // namespace QMdmmNetworking
