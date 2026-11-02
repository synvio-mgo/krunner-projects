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
 * The host focuses an open group or restores a recently closed one; when it
 * can do neither (the group left Chrome's recently-closed list) nothing is
 * created, the user gets a desktop notification instead.
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
    void handleOpenGroupReply(const QString &json);
    void openPlainWindow(const QStringList &urls);
    static void notify(const QString &summary, const QString &body);

    QString m_chromeCommand;
    QDBusServiceWatcher *m_watcher = nullptr;
};
