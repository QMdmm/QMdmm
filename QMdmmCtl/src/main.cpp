// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QMdmmGlobal>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpSocket>

#include <cstdlib>
#include <iostream>

using namespace Qt::StringLiterals;

namespace {

// How long the whole exchange gets: one connect, one request line, one answer. The control plane
// answers one short request per connection and closes, it is reached over the loopback interface,
// and it gives a peer that has not spoken five seconds before it drops it -- so this is longer than
// everything the far end does and short enough that a run pointed at nothing does not hang.
constexpr int answerTimeout = 5000;

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
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool connected = socket.waitForConnected(answerTimeout);
    if (!connected || socket.peerPort() != port) {
        *failure = u"nothing answered on 127.0.0.1:%1; is a server running there with --control-port?"_s.arg(static_cast<int>(port));
        return {};
    }

    socket.write("GET /status HTTP/1.0\r\n\r\n"_ba);
    socket.flush();

    QByteArray answer;
    for (;;) {
        if (socket.bytesAvailable() > 0) {
            answer += socket.readAll();
            continue;
        }
        if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
        if (!socket.waitForReadyRead(answerTimeout))
            break;
    }
    answer += socket.readAll();

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

    return answer.mid(headerEnd + 4);
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
