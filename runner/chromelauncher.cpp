#include "chromelauncher.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QProcess>
#include <QTimer>

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
    iface.asyncCall(u"OpenGroup"_s, guid);
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
