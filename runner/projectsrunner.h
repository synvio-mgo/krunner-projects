#pragma once

#include "savedgroups.h"

#include <KRunner/AbstractRunner>
#include <KRunner/Action>

#include <QElapsedTimer>
#include <QMutex>

class ChromeLauncher;

class ProjectsRunner : public KRunner::AbstractRunner
{
    Q_OBJECT
public:
    ProjectsRunner(QObject *parent, const KPluginMetaData &metaData);

    void match(KRunner::RunnerContext &context) override;
    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override;
    void reloadConfiguration() override;

private:
    enum class Mode { All, Projects, Groups };

    void refreshGroups(bool force = false);
    QStringList urlsForGroup(const QString &guid);
    void openCode(const QString &path);
    void openGroup(const QString &guid);
    static qreal relevanceFor(const QString &name, const QString &term);

    // configuration
    bool m_useTrigger = true;
    bool m_showGroups = true;
    QString m_projectTrigger;
    QString m_groupTrigger;
    QString m_codeCommand;
    QString m_chromeCommand;
    QString m_profileDir;

    // data
    QMutex m_mutex;
    QList<Project> m_projects;
    QList<SavedGroup> m_groups;
    QElapsedTimer m_groupsAge;

    QList<KRunner::Action> m_projectActions;
    ChromeLauncher *m_chrome;
};
