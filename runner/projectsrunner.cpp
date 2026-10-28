#include "projectsrunner.h"
#include "chromelauncher.h"
#include "config_keys.h"

#include <KLocalizedString>
#include <KRunner/RunnerContext>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

static const QString s_actionCode = u"code"_s;
static const QString s_actionChrome = u"chrome"_s;
static const QString s_actionBoth = u"both"_s;

ProjectsRunner::ProjectsRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_chrome(new ChromeLauncher(this))
{
    m_projectActions = {
        KRunner::Action(s_actionCode, u"vscode"_s, i18n("Open in VS Code only")),
        KRunner::Action(s_actionChrome, u"google-chrome"_s, i18n("Open Chrome group only")),
    };
    connect(this, &AbstractRunner::prepare, this, [this]() {
        refreshGroups();
    });
}

void ProjectsRunner::reloadConfiguration()
{
    const KConfigGroup cfg = config();
    m_useTrigger = cfg.readEntry(CONFIG_USE_TRIGGER, true);
    m_showGroups = cfg.readEntry(CONFIG_SHOW_GROUPS, true);
    m_projectTrigger = cfg.readEntry(CONFIG_PROJECT_TRIGGER, QStringLiteral(DEFAULT_PROJECT_TRIGGER)).trimmed();
    m_groupTrigger = cfg.readEntry(CONFIG_GROUP_TRIGGER, QStringLiteral(DEFAULT_GROUP_TRIGGER)).trimmed();
    m_codeCommand = cfg.readEntry(CONFIG_CODE_COMMAND, QStringLiteral(DEFAULT_CODE_COMMAND));
    m_chromeCommand = cfg.readEntry(CONFIG_CHROME_COMMAND, QStringLiteral(DEFAULT_CHROME_COMMAND));
    m_profileDir = cfg.readEntry(CONFIG_CHROME_PROFILE, QString());
    m_chrome->setChromeCommand(m_chromeCommand);

    {
        QMutexLocker lock(&m_mutex);
        m_projects = SavedGroups::readProjects(cfg);
    }

    QList<KRunner::RunnerSyntax> syntaxes;
    if (m_useTrigger) {
        QStringList words;
        if (!m_projectTrigger.isEmpty()) {
            words << QRegularExpression::escape(m_projectTrigger);
            syntaxes << KRunner::RunnerSyntax(m_projectTrigger + u" :q:"_s, i18n("Open a project: its VS Code folder and its Chrome tab group"));
        }
        if (m_showGroups && !m_groupTrigger.isEmpty()) {
            words << QRegularExpression::escape(m_groupTrigger);
            syntaxes << KRunner::RunnerSyntax(m_groupTrigger + u" :q:"_s, i18n("Open or focus a saved Chrome tab group"));
        }
        setMatchRegex(QRegularExpression(u"^(?:"_s + words.join(u'|') + u")(?:\\s|$)"_s, QRegularExpression::CaseInsensitiveOption));
        setMinLetterCount(1);
    } else {
        setMatchRegex(QRegularExpression());
        setMinLetterCount(2);
        syntaxes << KRunner::RunnerSyntax(u":q:"_s, i18n("Open a project or a saved Chrome tab group by name"));
    }
    setSyntaxes(syntaxes);
    refreshGroups(true);
}

void ProjectsRunner::refreshGroups(bool force)
{
    if (!force && m_groupsAge.isValid() && m_groupsAge.elapsed() < 15000) {
        return;
    }
    const QList<SavedGroup> groups = SavedGroups::load(m_profileDir);
    QMutexLocker lock(&m_mutex);
    m_groups = groups;
    m_groupsAge.restart();
}

qreal ProjectsRunner::relevanceFor(const QString &name, const QString &term)
{
    if (term.isEmpty()) {
        return 0.6;
    }
    if (name.compare(term, Qt::CaseInsensitive) == 0) {
        return 1.0;
    }
    if (name.startsWith(term, Qt::CaseInsensitive)) {
        return 0.9;
    }
    if (name.contains(term, Qt::CaseInsensitive)) {
        return 0.7;
    }
    // every word of the term somewhere in the name ("vdb tr" -> "VDB Transit")
    const QStringList words = term.split(u' ', Qt::SkipEmptyParts);
    if (words.size() > 1 && std::all_of(words.cbegin(), words.cend(), [&](const QString &w) {
            return name.contains(w, Qt::CaseInsensitive);
        })) {
        return 0.5;
    }
    return 0;
}

void ProjectsRunner::match(KRunner::RunnerContext &context)
{
    const QString query = context.query().trimmed();
    Mode mode = Mode::All;
    QString term = query;

    if (m_useTrigger) {
        const QString first = query.section(u' ', 0, 0);
        term = query.section(u' ', 1).trimmed();
        if (!m_projectTrigger.isEmpty() && first.compare(m_projectTrigger, Qt::CaseInsensitive) == 0) {
            mode = Mode::Projects;
        } else if (m_showGroups && !m_groupTrigger.isEmpty() && first.compare(m_groupTrigger, Qt::CaseInsensitive) == 0) {
            mode = Mode::Groups;
        } else {
            return;
        }
    }

    QList<Project> projects;
    QList<SavedGroup> groups;
    {
        QMutexLocker lock(&m_mutex);
        projects = m_projects;
        groups = m_groups;
    }

    QList<KRunner::QueryMatch> matches;

    if (mode != Mode::Groups) {
        for (const Project &p : projects) {
            const qreal rel = relevanceFor(p.name, term);
            if (rel <= 0) {
                continue;
            }
            KRunner::QueryMatch m(this);
            m.setId(u"project:"_s + p.name);
            m.setText(p.name);
            m.setIconName(p.icon.isEmpty() ? u"folder-development"_s : p.icon);
            m.setMatchCategory(i18n("Projects"));
            m.setRelevance(mode == Mode::All ? rel * 0.95 : rel);
            m.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);
            m.setData(QVariant::fromValue(QStringList{s_actionBoth, p.path, p.groupGuid}));

            QStringList parts;
            if (!p.path.isEmpty()) {
                parts << i18n("VS Code: %1", QDir::toNativeSeparators(p.path).replace(QDir::homePath(), u"~"_s));
            }
            if (!p.groupGuid.isEmpty()) {
                QString title = p.groupTitle;
                for (const SavedGroup &g : groups) {
                    if (g.guid == p.groupGuid) {
                        title = g.title;
                        break;
                    }
                }
                parts << i18n("Chrome: %1", title.isEmpty() ? i18n("(untitled group)") : title);
            }
            m.setSubtext(parts.join(u"  ·  "_s));

            QList<KRunner::Action> actions;
            if (!p.path.isEmpty() && !p.groupGuid.isEmpty()) {
                actions = m_projectActions;
            }
            m.setActions(actions);
            matches << m;
        }
    }

    if (mode != Mode::Projects && m_showGroups) {
        for (const SavedGroup &g : groups) {
            const QString title = g.title.isEmpty() ? i18n("(untitled group)") : g.title;
            const qreal rel = relevanceFor(title, term);
            if (rel <= 0) {
                continue;
            }
            KRunner::QueryMatch m(this);
            m.setId(u"group:"_s + g.guid);
            m.setText(title);
            m.setIconName(SavedGroups::colorIcon(g.color));
            m.setMatchCategory(i18n("Chrome tab groups"));
            m.setRelevance(mode == Mode::All ? rel * 0.8 : rel);
            m.setCategoryRelevance(mode == Mode::All ? KRunner::QueryMatch::CategoryRelevance::High : KRunner::QueryMatch::CategoryRelevance::Highest);
            m.setSubtext(i18np("%1 tab · %2", "%1 tabs · %2", g.tabCount, g.color));
            m.setData(QVariant::fromValue(QStringList{s_actionChrome, QString(), g.guid}));
            matches << m;
        }
    }

    context.addMatches(matches);
}

void ProjectsRunner::run(const KRunner::RunnerContext & /*context*/, const KRunner::QueryMatch &match)
{
    const QStringList data = match.data().toStringList();
    if (data.size() < 3) {
        return;
    }
    QString what = data.at(0);
    const QString path = data.at(1);
    const QString guid = data.at(2);

    const KRunner::Action selected = match.selectedAction();
    if (!selected.id().isEmpty()) {
        what = selected.id();
    }

    if ((what == s_actionBoth || what == s_actionCode) && !path.isEmpty()) {
        openCode(path);
    }
    if ((what == s_actionBoth || what == s_actionChrome) && !guid.isEmpty()) {
        openGroup(guid);
    }
}

void ProjectsRunner::openCode(const QString &path)
{
    QString cmd = m_codeCommand.trimmed();
    if (cmd.isEmpty()) {
        cmd = QStringLiteral(DEFAULT_CODE_COMMAND);
    }
    QStringList args = QProcess::splitCommand(cmd);
    const QString program = args.takeFirst();
    args << path;
    QProcess::startDetached(program, args, QFileInfo(path).isDir() ? path : QDir::homePath());
}

QStringList ProjectsRunner::urlsForGroup(const QString &guid)
{
    // Only needed for the fallback when the bridge is unavailable; ask the script for the full data.
    const QString script = SavedGroups::scriptPath();
    if (script.isEmpty()) {
        return {};
    }
    QStringList args{u"-I"_s, script, u"--json"_s};
    if (!m_profileDir.trimmed().isEmpty()) {
        args << u"--profile"_s << m_profileDir.trimmed();
    }
    QProcess proc;
    proc.start(u"python3"_s, args);
    if (!proc.waitForFinished(4000)) {
        return {};
    }
    const QJsonArray arr = QJsonDocument::fromJson(proc.readAllStandardOutput()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(u"guid"_s).toString() == guid) {
            QStringList urls;
            const QJsonArray tabs = o.value(u"tabs"_s).toArray();
            for (const QJsonValue &t : tabs) {
                urls << t.toObject().value(u"url"_s).toString();
            }
            return urls;
        }
    }
    return {};
}

void ProjectsRunner::openGroup(const QString &guid)
{
    const QStringList fallback = ChromeLauncher::serviceAvailable() ? QStringList() : urlsForGroup(guid);
    QMetaObject::invokeMethod(m_chrome, "openGroup", Qt::QueuedConnection, Q_ARG(QString, guid), Q_ARG(QStringList, fallback));
}

K_PLUGIN_CLASS_WITH_JSON(ProjectsRunner, "krunner_projects.json")

#include "projectsrunner.moc"
