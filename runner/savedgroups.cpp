#include "savedgroups.h"
#include "config_keys.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

QString SavedGroups::scriptPath()
{
    const QString rel = u"krunner-projects/chromegroups.py"_s;
    QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, rel);
    if (!path.isEmpty()) {
        return path;
    }
    path = QStringLiteral(KRUNNER_PROJECTS_DATADIR "/chromegroups.py");
    if (QFileInfo::exists(path)) {
        return path;
    }
    // development fallback: source tree next to the build dir
    const QString dev = QDir(QCoreApplication::applicationDirPath()).filePath(u"../host/chromegroups.py"_s);
    return QFileInfo::exists(dev) ? dev : QString();
}

QList<SavedGroup> SavedGroups::load(const QString &profileDir, int timeoutMs)
{
    QList<SavedGroup> groups;
    const QString script = scriptPath();
    if (script.isEmpty()) {
        return groups;
    }
    QStringList args{u"-I"_s, script, u"--json"_s};
    if (!profileDir.trimmed().isEmpty()) {
        args << u"--profile"_s << profileDir.trimmed();
    }
    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(u"python3"_s, args);
    if (!proc.waitForFinished(timeoutMs) || proc.exitCode() != 0) {
        return groups;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        SavedGroup g;
        g.guid = o.value(u"guid"_s).toString();
        g.title = o.value(u"title"_s).toString();
        g.color = o.value(u"color"_s).toString();
        g.tabCount = o.value(u"tabs"_s).toArray().size();
        groups << g;
    }
    return groups;
}

QString SavedGroups::colorIcon(const QString &color)
{
    // Chrome colours mapped onto Breeze folder icons, which exist for every colour.
    static const QHash<QString, QString> map{
        {u"grey"_s, u"folder-grey"_s},     {u"blue"_s, u"folder-blue"_s},   {u"red"_s, u"folder-red"_s},
        {u"yellow"_s, u"folder-yellow"_s}, {u"green"_s, u"folder-green"_s}, {u"pink"_s, u"folder-magenta"_s},
        {u"purple"_s, u"folder-violet"_s}, {u"cyan"_s, u"folder-cyan"_s},   {u"orange"_s, u"folder-orange"_s},
    };
    return map.value(color, u"google-chrome"_s);
}

QList<Project> SavedGroups::readProjects(const KConfigGroup &runnerGroup)
{
    QList<Project> projects;
    const KConfigGroup all = runnerGroup.group(QStringLiteral(CONFIG_PROJECTS_GROUP));
    const QStringList names = all.groupList();
    for (const QString &name : names) {
        const KConfigGroup g = all.group(name);
        Project p;
        p.name = name;
        p.path = g.readEntry(PROJECT_KEY_PATH, QString());
        p.groupGuid = g.readEntry(PROJECT_KEY_GROUP, QString());
        p.groupTitle = g.readEntry(PROJECT_KEY_GROUP_TITLE, QString());
        p.icon = g.readEntry(PROJECT_KEY_ICON, QString());
        projects << p;
    }
    std::sort(projects.begin(), projects.end(), [](const Project &a, const Project &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return projects;
}

void SavedGroups::writeProjects(KConfigGroup &runnerGroup, const QList<Project> &projects)
{
    KConfigGroup all = runnerGroup.group(QStringLiteral(CONFIG_PROJECTS_GROUP));
    const QStringList old = all.groupList();
    for (const QString &name : old) {
        all.deleteGroup(name);
    }
    for (const Project &p : projects) {
        KConfigGroup g = all.group(p.name);
        g.writeEntry(PROJECT_KEY_PATH, p.path);
        g.writeEntry(PROJECT_KEY_GROUP, p.groupGuid);
        g.writeEntry(PROJECT_KEY_GROUP_TITLE, p.groupTitle);
        g.writeEntry(PROJECT_KEY_ICON, p.icon);
    }
    all.sync();
}
