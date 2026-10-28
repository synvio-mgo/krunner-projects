#pragma once

#include "savedgroups.h"

#include <KCModule>

class QCheckBox;
class QLineEdit;
class QTableWidget;
class QPushButton;

class ProjectsConfig : public KCModule
{
    Q_OBJECT
public:
    explicit ProjectsConfig(QObject *parent, const KPluginMetaData &data);

    void load() override;
    void save() override;
    void defaults() override;

private:
    void buildUi();
    void refreshGroups();
    void addProject();
    void editProject();
    void removeProject();
    void fillTable();
    bool editProjectDialog(Project &project, bool isNew);
    void updateButtons();

    QCheckBox *m_useTrigger = nullptr;
    QLineEdit *m_projectTrigger = nullptr;
    QLineEdit *m_groupTrigger = nullptr;
    QCheckBox *m_showGroups = nullptr;
    QLineEdit *m_codeCommand = nullptr;
    QLineEdit *m_chromeCommand = nullptr;
    QLineEdit *m_profileDir = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_refreshButton = nullptr;

    QList<Project> m_projects;
    QList<SavedGroup> m_groups;
};
