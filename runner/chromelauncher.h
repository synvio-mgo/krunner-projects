#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QDBusServiceWatcher;

/**
 * Talks to the Chrome extension through the native messaging host's D-Bus
 * service.  If Chrome is not running yet it is started first, and if the
 * service never shows up the group's urls are opened in a plain window.
 *
 * Lives in the main thread; call openGroup via a queued invocation.
 */
class ChromeLauncher : public QObject
{
    Q_OBJECT
public:
    explicit ChromeLauncher(QObject *parent = nullptr);

    void setChromeCommand(const QString &cmd);
    static bool serviceAvailable();

public Q_SLOTS:
    void openGroup(const QString &guid, const QStringList &fallbackUrls);

private:
    void callOpenGroup(const QString &guid);
    void openPlainWindow(const QStringList &urls);

    QString m_chromeCommand;
    QDBusServiceWatcher *m_watcher = nullptr;
};
