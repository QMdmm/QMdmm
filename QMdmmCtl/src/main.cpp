// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QMdmmGlobal>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>

#include <cstdlib>
#include <iostream>

using namespace Qt::StringLiterals;

namespace {

// How long the whole exchange gets: one connect, one request line, one answer. The control plane
// answers one short request per connection and closes, it is reached over the loopback interface,
// and it gives a peer that has not spoken five seconds before it drops it -- so this is longer than
// everything the far end does and short enough that a run pointed at nothing does not hang.
//
// It is one deadline for the exchange and not one per read: a peer that drips a byte at a time
// would never run out of reads, and a run any peer can keep waiting for as long as it likes is a
// run whose caller has no bound to lean on.
constexpr int answerTimeout = 5000;

// The most of an answer this program will hold. The snapshot is a few hundred bytes and grows with
// the number of rooms rather than with anything the peer chooses; an answer past this is a peer
// that is sending rather than answering, and how far the buffer behind it would grow is its choice
// rather than this program's.
constexpr qsizetype answerSizeLimit = static_cast<qsizetype>(1024) * 1024;

// What a rejected run ends with: the same 3 the two long-running programs use for a configuration
// they will not run with. A caller has to be able to tell "the arguments were refused" apart from
// "the arguments were fine and no answer came".
constexpr int exitRejectedRun = 3;

// What a run ends with when the question could not be put, or the answer was not the snapshot.
// Non-zero, because nothing was read -- and distinct from 3, which is decided before anything is
// asked.
constexpr int exitNoAnswer = 1;

QString helpText()
{
    return uR"(Usage: QMdmmCtl6 <command> [options]

Commands:
  status                             Print the server's status snapshot, byte for byte as
                                     the control plane serves it.

Options:
  -p, --control-port <port>          The port the server's control plane listens on, i.e. the
                                     value that server was given with --control-port.
  -h, --help                         Print this table and exit.
  -v, --version                      Print the version and exit.

The control plane is off on a server that was not given a port for it, so there is no default
to fall back to: status always needs --control-port.

Starting, stopping and enabling a server through the platform's own service manager is not
implemented yet; this program only asks.
)"_s;
}

// The one way this program gives up.
//
// The reason goes to stderr and nowhere else. The two long-running programs also repeat it through
// qWarning(), because their message handler is already writing a log file and that is how the same
// line reaches both places; this program installs no handler and keeps no log, so a qWarning() here
// would only print the same line on the same stream a second time.
[[noreturn]] void giveUp(const QString &message, int exitCode)
{
    std::cerr << qPrintable(message) << '\n' << std::flush;
    std::exit(exitCode);
}

[[noreturn]] void reject(const QString &message)
{
    giveUp(message, exitRejectedRun);
}

// Whether what came back is the snapshot the control plane serves.
//
// A 200 is not enough on its own: this plane answers what it does not serve with a 404 and its own
// text, but a peer that is not this plane at all is free to answer 200 with nothing behind it, and
// that would be printed as a snapshot and reported as a healthy server to whoever reads the output.
//
// The four keys are asked for by name and nothing beyond them: what the program prints is the body
// byte for byte, so a key it does not know about is carried through as it arrived. A body missing
// one of the four is a different shape, not a newer one.
bool isTheSnapshot(const QByteArray &body)
{
    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject snapshot = document.object();
    return snapshot.value(u"rooms"_s).isDouble() && snapshot.value(u"games"_s).isDouble() && snapshot.value(u"players"_s).isDouble() && snapshot.value(u"roomList"_s).isArray();
}

// One HTTP/1.0 exchange with the control plane, and the body that came back.
//
// The request line is the whole of what that plane looks at, and it answers exactly one request
// per connection and then closes -- so the close is what says the answer is complete, and there is
// nothing to negotiate or to keep alive.
//
// A failed connection is not told apart from an unreachable one here. Qt reports a successful
// connection to a loopback port with nothing listening on this platform as a state that looks made
// (peer port 0, no peer address), so the peer port is read instead: an attempt that ends with the
// port this was asked for is one the far end took, and anything else never got there.
QByteArray fetchSnapshot(quint16 port, QString *failure)
{
    // Started before the connection is attempted: a peer that is slow to accept and one that is
    // slow to answer are the same wait to the caller this belongs to.
    QElapsedTimer deadline;
    deadline.start();
    const auto timeLeft = [&deadline] {
        const qint64 left = answerTimeout - deadline.elapsed();
        return left > 0 ? static_cast<int>(left) : 0;
    };

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool connected = socket.waitForConnected(timeLeft());
    if (!connected || socket.peerPort() != port) {
        *failure = u"nothing answered on 127.0.0.1:%1; is a server running there with --control-port?"_s.arg(static_cast<int>(port));
        return {};
    }

    socket.write("GET /status HTTP/1.0\r\n\r\n"_ba);
    socket.flush();

    // Only the close ends the loop. Running out of the deadline does not count as an answer that
    // ended: a peer that holds the connection open and says nothing is the one shape a caller
    // cannot tell from a slow answer, so it is reported as no answer rather than parsed as one.
    QByteArray answer;
    for (;;) {
        if (socket.bytesAvailable() > 0) {
            answer += socket.readAll();
            if (answer.size() > answerSizeLimit) {
                *failure = u"the answer from 127.0.0.1:%1 grew past %2 bytes without ending"_s.arg(static_cast<int>(port)).arg(static_cast<qlonglong>(answerSizeLimit));
                return {};
            }
            continue;
        }
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;

        // A wait that came back empty is either the far end having closed or the deadline running
        // out, and the two are not the same answer: the close is what says an answer is complete,
        // so the state is read rather than the return value trusted. A deadline of zero is the same
        // statement a moment later, and is not worth a wait of its own.
        const int left = timeLeft();
        if (left == 0 || (!socket.waitForReadyRead(left) && socket.state() != QAbstractSocket::UnconnectedState)) {
            *failure = u"the answer from 127.0.0.1:%1 did not finish within %2 ms"_s.arg(static_cast<int>(port)).arg(answerTimeout);
            return {};
        }
    }

    const qsizetype headerEnd = answer.indexOf("\r\n\r\n"_ba);
    if (headerEnd < 0) {
        *failure = u"the answer from 127.0.0.1:%1 ended before its header block did"_s.arg(static_cast<int>(port));
        return {};
    }

    // Only the status line is looked at, and only for the one answer that carries a snapshot: this
    // plane answers a path or a method it does not serve with a 404 and its own text, and passing
    // that on as if it were the snapshot would report a mistyped request as a healthy server.
    const QByteArray statusLine = answer.left(answer.indexOf("\r\n"_ba));
    if (!statusLine.startsWith("HTTP/1.0 200 "_ba)) {
        *failure = u"127.0.0.1:%1 answered '%2' rather than the snapshot"_s.arg(static_cast<int>(port)).arg(QString::fromLatin1(statusLine));
        return {};
    }

    QByteArray body = answer.mid(headerEnd + 4);
    if (!isTheSnapshot(body)) {
        *failure = u"127.0.0.1:%1 answered 200 with something that is not the snapshot"_s.arg(static_cast<int>(port));
        return {};
    }

    return body;
}

// The body goes out exactly as it arrived, down to the absence of a trailing newline.
//
// What this output is for is being compared with the same answer read any other way -- `curl -s
// 127.0.0.1:<port>/status`, say -- so a newline added here would be the one difference a reader
// would have to explain. The plane's body carries none, and neither does this.
void writeSnapshot(const QByteArray &body)
{
    std::cout.write(body.constData(), static_cast<std::streamsize>(body.size()));
    std::cout << std::flush;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setOrganizationName(u"Fsu0413.me"_s);
    QCoreApplication::setApplicationName(u"QMdmmCtl"_s);
    QCoreApplication::setApplicationVersion(QMdmmCore::Global::version().toString());

    QCommandLineParser parser;
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsCompactedShortOptions);

    // parser.addHelpOption() builds a text of its own, and what a user reads here is the table
    // above: the same choice the server's own command line makes.
    parser.addOption(QCommandLineOption(QStringList {u"h"_s, u"help"_s}));
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList {u"p"_s, u"control-port"_s}, {}, u"port"_s));
    parser.addPositionalArgument(u"command"_s, u"the command to run; only 'status' exists"_s);

    parser.process(a);

    if (parser.isSet(u"h"_s)) {
        std::cout << qPrintable(helpText()) << std::flush;
        std::exit(0);
    }

    const QStringList commands = parser.positionalArguments();
    if (commands.isEmpty())
        reject(u"No command given. Run with --help for the list."_s);
    if (commands.size() > 1)
        reject(u"One command at a time; got: %1. Run with --help for the list."_s.arg(commands.join(u", "_s)));

    const QString &command = commands.first();
    if (command != u"status"_s)
        reject(u"Unknown command: %1. Run with --help for the list."_s.arg(command));

    const QString portText = parser.value(u"control-port"_s);
    if (portText.isEmpty())
        reject(u"status needs --control-port: a server without one has no control plane to ask."_s);

    bool portIsANumber = false;
    const uint port = portText.toUInt(&portIsANumber);
    if (!portIsANumber || port == 0 || port > 65535)
        reject(u"--control-port takes a port number, not '%1'."_s.arg(portText));

    QString failure;
    const QByteArray snapshot = fetchSnapshot(static_cast<quint16>(port), &failure);
    if (!failure.isEmpty())
        giveUp(failure, exitNoAnswer);

    writeSnapshot(snapshot);
    return 0;
}
