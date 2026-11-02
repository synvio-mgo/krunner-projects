#include "chromelauncher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QProcess>
#include <QTimer>

#include <KLocalizedString>

using namespace Qt::StringLiterals;

static const QString s_service = u"be.synvio.KRunnerProjects"_s;
static const QString s_path = u"/Chrome"_s;
static const QString s_iface = u"be.synvio.KRunnerProjects.Chrome"_s;

ChromeLauncher::ChromeLauncher(QObject *parent)
    : QObject(parent)
{
}

void ChromeLauncher::setChromeCommand(const QString &cmd)
{
    m_chromeCommand = cmd.trimmed().isEmpty() ? u"google-chrome"_s : cmd.trimmed();
}

bool ChromeLauncher::serviceAvailable()
{
    const auto *iface = QDBusConnection::sessionBus().interface();
    return iface && iface->isServiceRegistered(s_service);
}

void ChromeLauncher::openGroup(const QString &guid, const QStringList &fallbackUrls)
{
    if (serviceAvailable()) {
        callOpenGroup(guid);
        return;
    }

    // Chrome (or the extension) is not up: start Chrome and wait for the host.
    qInfo() << "krunner_projects: starting" << m_chromeCommand << "and waiting for the bridge";
    QProcess::startDetached(m_chromeCommand, {});

    if (m_watcher) {
        m_watcher->deleteLater();
    }
    m_watcher = new QDBusServiceWatcher(s_service, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForRegistration, this);
    auto *timeout = new QTimer(m_watcher);
    timeout->setSingleShot(true);

    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, [this, guid, timeout]() {
        timeout->stop();
        // give the host a moment to finish its hello handshake
        QTimer::singleShot(400, this, [this, guid]() {
            callOpenGroup(guid);
        });
        m_watcher->deleteLater();
        m_watcher = nullptr;
    });
    connect(timeout, &QTimer::timeout, this, [this, fallbackUrls]() {
        qWarning() << "krunner_projects: bridge did not appear, opening urls in a plain window";
        openPlainWindow(fallbackUrls);
        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = nullptr;
        }
    });
    timeout->start(12000);
}

void ChromeLauncher::callOpenGroup(const QString &guid)
{
    QDBusInterface iface(s_service, s_path, s_iface, QDBusConnection::sessionBus());
    iface.setTimeout(30000); // restoring may take a few seconds
    auto *watcher = new QDBusPendingCallWatcher(iface.asyncCall(u"OpenGroup"_s, guid), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            qWarning() << "krunner_projects: OpenGroup failed:" << reply.error().message();
            notify(i18n("Chrome group not opened"), reply.error().message());
            return;
        }
        handleOpenGroupReply(reply.value());
    });
}

void ChromeLauncher::handleOpenGroupReply(const QString &json)
{
    const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8()).object();
    if (obj.value(u"ok"_s).toBool()) {
        return;
    }
    const QString title = obj.value(u"title"_s).toString();
    const QString reason = obj.value(u"reason"_s).toString();
    qInfo() << "krunner_projects: group" << title << "not opened:" << json;
    if (reason == u"closed_not_restorable"_s) {
        notify(i18n("Chrome group \"%1\" not opened", title),
               i18n("It was closed too long ago for Chrome to restore it. "
                    "Open it once from Chrome's tab group bar; after that KRunner can open it again."));
    } else {
        const QString err = obj.value(u"error"_s).toString();
        notify(i18n("Chrome group \"%1\" not opened", title), err.isEmpty() ? reason : err);
    }
}

void ChromeLauncher::notify(const QString &summary, const QString &body)
{
    QDBusInterface iface(u"org.freedesktop.Notifications"_s, u"/org/freedesktop/Notifications"_s,
                         u"org.freedesktop.Notifications"_s, QDBusConnection::sessionBus());
    iface.asyncCall(u"Notify"_s, u"krunner"_s, 0u, u"google-chrome"_s, summary, body, QStringList(), QVariantMap(), 8000);
}

void ChromeLauncher::openPlainWindow(const QStringList &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    QStringList args{u"--new-window"_s};
    args << urls;
    QProcess::startDetached(m_chromeCommand, args);
}
