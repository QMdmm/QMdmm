// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class QQuickWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // serverProgram / botProgram are where a local game's server and bot are to be started
    // from, empty to look next to this program; see QMdmmGameClient::setProgramPaths.
    explicit MainWindow(const QString &serverProgram, const QString &botProgram, QWidget *parent = nullptr);
    ~MainWindow() override;

signals:

private:
    // The view of the scene, held so that the destructor can take it down first; the order
    // is what the comments at both ends of it are about.
    QQuickWidget *m_view;
};

#endif // MAINWINDOW_H
