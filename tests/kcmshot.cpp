// Loads the KCM offscreen and writes a screenshot, so the layout can be checked
// without opening a window:  QT_QPA_PLATFORM=offscreen kcmshot <kcm.so> out.png
#include <KCModule>
#include <KCModuleLoader>
#include <KPluginMetaData>

#include <QApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 3) {
        out << "usage: kcmshot <kcm.so> <out.png>\n";
        return 2;
    }
    KCModule *module = KCModuleLoader::loadModule(KPluginMetaData(QString::fromLocal8Bit(argv[1])));
    if (!module) {
        out << "failed to load KCM\n";
        return 1;
    }
    module->load();
    QWidget *w = module->widget();
    w->resize(860, 640);
    w->show();
    int rc = 1;
    QTimer::singleShot(500, &app, [&]() {
        rc = w->grab().save(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
        out << (rc == 0 ? "saved " : "could not save ") << argv[2] << "\n";
        out.flush();
        app.quit();
    });
    app.exec();
    return rc;
}
