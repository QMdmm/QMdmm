// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QQuickWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // A window is built and destroyed in place; the client behind its view is a child of it,
    // which is what the destructor is for, and copying either would leave two of them.
    Q_DISABLE_COPY_MOVE(MainWindow)

    // serverProgram / botProgram are where a local game's server and bot are to be started
    // from, empty to look next to this program; see QMdmmGameClient::setProgramPaths.
    explicit MainWindow(const QString &serverProgram, const QString &botProgram, QWidget *parent = nullptr);
    ~MainWindow() override;

signals:

protected:
    // The size the window is left at is written out here, for the constructor to open the
    // next run at. The setter only fills the specified settings instance, so what reaches the
    // per-user file follows from the save call in this hook.
    void closeEvent(QCloseEvent *event) override;

private:
    // The view of the scene, held so that the destructor can take it down first; the order
    // is what the comments at both ends of it are about.
    QQuickWidget *m_view;
};

#endif // MAINWINDOW_H
