// Drives the runner plugin through KRunner::RunnerManager without touching the
// live krunner.  Config comes from the file named in KRUNNER_PROJECTS_TEST_CONFIG
// (a krunnerrc-style file), so projects can be seeded for the test.
#include <KConfigGroup>
#include <KPluginMetaData>
#include <KRunner/AbstractRunner>
#include <KRunner/QueryMatch>
#include <KRunner/RunnerManager>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 3) {
        out << "usage: runnertest <plugin.so> <query>...\n";
        return 2;
    }
    const QString configFile = qEnvironmentVariable("KRUNNER_PROJECTS_TEST_CONFIG", u"krunnerrc"_s);
    auto config = KSharedConfig::openConfig(configFile);
    KRunner::RunnerManager manager(config->group(u"Plugins"_s), config->group(u"PlasmaRunnerManager"_s), nullptr);
    manager.setAllowedRunners({u"krunner_projects"_s});

    KRunner::AbstractRunner *runner = manager.loadRunner(KPluginMetaData(QString::fromLocal8Bit(argv[1])));
    if (!runner) {
        out << "failed to load plugin\n";
        return 1;
    }
    out << "loaded runner " << runner->id() << " (" << runner->name() << ")\n";

    int failures = 0;
    for (int i = 2; i < argc; ++i) {
        const QString query = QString::fromLocal8Bit(argv[i]);
        QEventLoop loop;
        QObject::connect(&manager, &KRunner::RunnerManager::queryFinished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        manager.launchQuery(query);
        loop.exec();
        const QList<KRunner::QueryMatch> matches = manager.matches();
        out << "query \"" << query << "\": " << matches.size() << " match(es)\n";
        for (const KRunner::QueryMatch &m : matches) {
            out << "   " << QString::number(m.relevance(), 'f', 2) << "  [" << m.matchCategory() << "] " << m.text() << "  —  " << m.subtext()
                << "  (" << m.iconName() << ", " << m.actions().size() << " actions)\n";
        }
        if (matches.isEmpty()) {
            ++failures;
        }
    }
    out.flush();
    return failures;
}
