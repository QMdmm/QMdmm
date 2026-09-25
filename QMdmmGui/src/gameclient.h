// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef QMDMMGUI_GAMECLIENT_H_
#define QMDMMGUI_GAMECLIENT_H_

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <QMdmmClient>
#include <QMdmmPlayer>
#include <QMdmmRoom>

QMDMM_EXPORT_NAME(QMdmmGameClient)

class QMdmmGameClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList players READ players NOTIFY playersChanged)
    Q_PROPERTY(QString gameState READ gameState NOTIFY gameStateChanged)
    Q_PROPERTY(QString localName READ localName NOTIFY localNameChanged)
    Q_PROPERTY(QVariantList chatLog READ chatLog NOTIFY chatLogChanged)
    Q_PROPERTY(QVariantMap agentStates READ agentStates NOTIFY agentStatesChanged)
    Q_PROPERTY(QVariantMap logicConfiguration READ logicConfiguration NOTIFY logicConfigurationChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(int playerCount READ playerCount WRITE setPlayerCount NOTIFY playerCountChanged)
    Q_PROPERTY(QString serverProgram READ serverProgram NOTIFY programPathsChanged)
    Q_PROPERTY(QString botProgram READ botProgram NOTIFY programPathsChanged)

public:
    Q_DISABLE_COPY_MOVE(QMdmmGameClient)

    enum class GameState : uint8_t
    {
        Start,
        Lobby,
        Playing,
        GameOver,
    };
    Q_ENUM(GameState)

    explicit QMdmmGameClient(QObject *parent = nullptr);
    ~QMdmmGameClient() override;

    [[nodiscard]] QVariantList players() const;
    [[nodiscard]] QString gameState() const;
    [[nodiscard]] QString localName() const;
    [[nodiscard]] QVariantList chatLog() const;
    [[nodiscard]] QVariantMap agentStates() const;
    [[nodiscard]] QVariantMap logicConfiguration() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] int playerCount() const;
    void setPlayerCount(int n);

    // Where the two programs a local game needs live: the server that owns the room and
    // the bot that fills a seat. Both are resolved once, from the path given on the command
    // line if there is one and from next to this program otherwise (see setProgramPaths).
    // An empty path means "not found" -- not an error yet, because nothing needs either
    // program until a local game is asked for.
    [[nodiscard]] QString serverProgram() const;
    [[nodiscard]] QString botProgram() const;
    Q_INVOKABLE void setProgramPaths(const QString &serverProgram, const QString &botProgram);

    Q_INVOKABLE void startLocalGame(const QString &playerName);
    Q_INVOKABLE void connectOnline(const QString &host, const QString &playerName);
    Q_INVOKABLE void disconnectAll();

    // Request replies (for the human player)
    Q_INVOKABLE void replyRps(int rps);
    Q_INVOKABLE void replyActionOrder(const QVariantList &orders);
    Q_INVOKABLE void yieldActionOrder(int selectionNum);
    Q_INVOKABLE void replyAction(int action, const QString &toPlayer, int toPlace);
    Q_INVOKABLE void replyUpgrade(const QVariantList &items);
    Q_INVOKABLE void speak(const QString &text);

    // The managed flag (Data::StateMaskTrust) of the human's own player -- the one piece of an
    // agent's state that is set from this side rather than only read. A managed player picks
    // nothing: every request is given up on its behalf and the peer answers with its own default
    // reply, so the round goes on and the upgrade point still gets spent (see
    // requestIsForTheHuman).
    Q_INVOKABLE void setManaged(bool managed);

    // Helpers for the action / upgrade UI
    [[nodiscard]] Q_INVOKABLE QVariantList getActionOptions() const;
    [[nodiscard]] Q_INVOKABLE QVariantList getUpgradeOptions() const;

    // Display-name lookups (the Room model only stores the internal player name)
    [[nodiscard]] Q_INVOKABLE QString screenName(const QString &playerName) const;
    [[nodiscard]] Q_INVOKABLE bool isYou(const QString &playerName) const;
    [[nodiscard]] Q_INVOKABLE QString placeName(int place) const;

signals:
    void playersChanged();
    void gameStateChanged();
    void localNameChanged();
    void chatLogChanged();
    void agentStatesChanged();
    void logicConfigurationChanged();
    void statusMessageChanged(const QString &);
    void playerCountChanged();
    void programPathsChanged();

    void requestRockPaperScissors(const QStringList &playerNames, int strivedOrder);
    void requestActionOrder(const QList<int> &remainedOrders, int maximumOrder, int selectionNum);
    void requestAction(int currentOrder);
    void requestUpgrade(int remainingTimes);
    // A request the view is still showing has been given up on the player's behalf, because the
    // player is managed (see setManaged): it is no longer open, so the overlay asking for it has
    // to come down.
    void requestWithdrawn();

    void playerAdded(const QString &playerName, const QString &screenName, int agentState);
    void playerRemoved(const QString &playerName);
    void gameStart();
    void roundStart();
    void roundOver();
    void rpsResult(const QVariantMap &results);
    void actionOrderResult(const QVariantMap &result);
    void actionResult(const QString &playerName, int action, const QString &toPlayer, int toPlace);
    void upgradeResult(const QVariantMap &upgrades);
    void gameOver(const QStringList &winners);
    void spoken(const QString &playerName, const QString &content);
    void errorOccurred(const QString &msg);

private:
    void wireClient(QMdmmNetworking::Client *client);
    void addBot(const QString &name);
    void reset();
    void stopLocalServer();
    void stopBots();
    void setGameState(GameState s);
    void setStatusMessage(const QString &msg);
    void setLogicConfiguration(const QMdmmCore::LogicConfiguration &conf);
    [[nodiscard]] bool requestIsForTheHuman();
    [[nodiscard]] QMdmmCore::Player *localPlayer() const;
    QVariantList actionListFor(const QMdmmCore::Player *from) const;
    [[nodiscard]] static QString locateProgram(const QString &programName, const QString &explicitPath);

    QMdmmNetworking::Client *m_human = nullptr;
    QProcess *m_serverProcess = nullptr;
    // One QMdmmBot process per seat this side fills, stopped with the game they belong to.
    QList<QProcess *> m_bots;
    QMdmmCore::Room *m_room = nullptr;

    QString m_localName;
    QString m_localScreen;
    GameState m_state = GameState::Start;
    QVariantList m_chat;
    QVariantMap m_logicConfiguration;
    QString m_status;
    int m_playerCount = 3;
    QString m_serverProgram;
    QString m_botProgram;

    QHash<QString, QString> m_screenNames;
    QHash<QString, QMdmmCore::Data::AgentState> m_agentStates;
};

#endif // QMDMMGUI_GAMECLIENT_H_
