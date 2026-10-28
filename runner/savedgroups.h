#pragma once

#include <KConfigGroup>
#include <QList>
#include <QString>

struct SavedGroup {
    QString guid;
    QString title;
    QString color; // chrome colour name: grey, blue, red, ...
    int tabCount = 0;
};

struct Project {
    QString name;
    QString path;       // folder or .code-workspace file; may be empty
    QString groupGuid;  // saved Chrome group; may be empty
    QString groupTitle; // cached for display
    QString icon;
};

namespace SavedGroups
{
/// Path of chromegroups.py (installed data dir, or source tree during development).
QString scriptPath();

/// Runs chromegroups.py synchronously (timeout in ms). profileDir may be empty for auto-detect.
QList<SavedGroup> load(const QString &profileDir, int timeoutMs = 4000);

/// Icon name that roughly matches a chrome group colour.
QString colorIcon(const QString &color);

/// Projects stored under the runner's config group.
QList<Project> readProjects(const KConfigGroup &runnerGroup);
void writeProjects(KConfigGroup &runnerGroup, const QList<Project> &projects);
}
