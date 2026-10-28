#include "projectsconfig.h"
#include "config_keys.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

K_PLUGIN_CLASS_WITH_JSON(ProjectsConfig, "kcm_krunner_projects.json")

static KConfigGroup runnerConfig()
{
    return KSharedConfig::openConfig(u"krunnerrc"_s)->group(u"Runners"_s).group(QStringLiteral(KRUNNER_PLUGIN_NAME));
}

ProjectsConfig::ProjectsConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    buildUi();
}

void ProjectsConfig::buildUi()
{
    auto *layout = new QVBoxLayout(widget());

    // --- trigger ---------------------------------------------------------
    auto *triggerBox = new QGroupBox(i18n("How to trigger"), widget());
    auto *triggerForm = new QFormLayout(triggerBox);
    m_useTrigger = new QCheckBox(i18n("Only match after a trigger word"), triggerBox);
    m_projectTrigger = new QLineEdit(triggerBox);
    m_projectTrigger->setPlaceholderText(QStringLiteral(DEFAULT_PROJECT_TRIGGER));
    m_groupTrigger = new QLineEdit(triggerBox);
    m_groupTrigger->setPlaceholderText(QStringLiteral(DEFAULT_GROUP_TRIGGER));
    m_showGroups = new QCheckBox(i18n("Also list saved Chrome tab groups on their own"), triggerBox);
    triggerForm->addRow(m_useTrigger);
    triggerForm->addRow(i18n("Projects, e.g. \"p odoo\":"), m_projectTrigger);
    triggerForm->addRow(i18n("Chrome groups, e.g. \"cg vdb\":"), m_groupTrigger);
    triggerForm->addRow(m_showGroups);
    auto *hint = new QLabel(i18n("Without trigger words, just type the project or group name."), triggerBox);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    triggerForm->addRow(hint);
    layout->addWidget(triggerBox);

    // --- commands --------------------------------------------------------
    auto *cmdBox = new QGroupBox(i18n("Commands"), widget());
    auto *cmdForm = new QFormLayout(cmdBox);
    m_codeCommand = new QLineEdit(cmdBox);
    m_codeCommand->setPlaceholderText(QStringLiteral(DEFAULT_CODE_COMMAND));
    m_chromeCommand = new QLineEdit(cmdBox);
    m_chromeCommand->setPlaceholderText(QStringLiteral(DEFAULT_CHROME_COMMAND));
    auto *profileRow = new QWidget(cmdBox);
    auto *profileLayout = new QHBoxLayout(profileRow);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    m_profileDir = new QLineEdit(profileRow);
    m_profileDir->setPlaceholderText(i18n("auto-detect (~/.config/google-chrome/Default)"));
    auto *browseProfile = new QPushButton(QIcon::fromTheme(u"folder-open"_s), QString(), profileRow);
    profileLayout->addWidget(m_profileDir);
    profileLayout->addWidget(browseProfile);
    cmdForm->addRow(i18n("VS Code command:"), m_codeCommand);
    cmdForm->addRow(i18n("Chrome command:"), m_chromeCommand);
    cmdForm->addRow(i18n("Chrome profile folder:"), profileRow);
    layout->addWidget(cmdBox);

    connect(browseProfile, &QPushButton::clicked, this, [this]() {
        const QString start = m_profileDir->text().isEmpty() ? QDir::homePath() + u"/.config/google-chrome"_s : m_profileDir->text();
        const QString dir = QFileDialog::getExistingDirectory(widget(), i18n("Choose the Chrome profile folder"), start);
        if (!dir.isEmpty()) {
            m_profileDir->setText(dir);
            refreshGroups();
        }
    });

    // --- projects --------------------------------------------------------
    auto *projBox = new QGroupBox(i18n("Projects"), widget());
    auto *projLayout = new QVBoxLayout(projBox);
    m_table = new QTableWidget(0, 3, projBox);
    m_table->setHorizontalHeaderLabels({i18n("Name"), i18n("VS Code folder / workspace"), i18n("Chrome tab group")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    projLayout->addWidget(m_table);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(QIcon::fromTheme(u"list-add"_s), i18n("Add…"), projBox);
    m_editButton = new QPushButton(QIcon::fromTheme(u"document-edit"_s), i18n("Edit…"), projBox);
    m_removeButton = new QPushButton(QIcon::fromTheme(u"list-remove"_s), i18n("Remove"), projBox);
    m_refreshButton = new QPushButton(QIcon::fromTheme(u"view-refresh"_s), i18n("Reload Chrome groups"), projBox);
    buttons->addWidget(addButton);
    buttons->addWidget(m_editButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch();
    buttons->addWidget(m_refreshButton);
    projLayout->addLayout(buttons);
    layout->addWidget(projBox, 1);

    connect(addButton, &QPushButton::clicked, this, &ProjectsConfig::addProject);
    connect(m_editButton, &QPushButton::clicked, this, &ProjectsConfig::editProject);
    connect(m_removeButton, &QPushButton::clicked, this, &ProjectsConfig::removeProject);
    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        refreshGroups();
        fillTable();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ProjectsConfig::updateButtons);
    connect(m_table, &QTableWidget::itemDoubleClicked, this, &ProjectsConfig::editProject);

    // --- change tracking -------------------------------------------------
    connect(m_useTrigger, &QCheckBox::toggled, this, [this](bool on) {
        m_projectTrigger->setEnabled(on);
        m_groupTrigger->setEnabled(on);
        markAsChanged();
    });
    connect(m_showGroups, &QCheckBox::toggled, this, &ProjectsConfig::markAsChanged);
    for (QLineEdit *edit : {m_projectTrigger, m_groupTrigger, m_codeCommand, m_chromeCommand, m_profileDir}) {
        connect(edit, &QLineEdit::textChanged, this, &ProjectsConfig::markAsChanged);
    }
    updateButtons();
}

void ProjectsConfig::refreshGroups()
{
    m_groups = SavedGroups::load(m_profileDir->text());
    m_refreshButton->setToolTip(m_groups.isEmpty() ? i18n("No saved tab groups found. Is the profile folder right?")
                                                   : i18np("%1 saved tab group found", "%1 saved tab groups found", m_groups.size()));
}

void ProjectsConfig::load()
{
    KCModule::load();
    const KConfigGroup grp = runnerConfig();
    m_useTrigger->setChecked(grp.readEntry(CONFIG_USE_TRIGGER, true));
    m_showGroups->setChecked(grp.readEntry(CONFIG_SHOW_GROUPS, true));
    m_projectTrigger->setText(grp.readEntry(CONFIG_PROJECT_TRIGGER, QStringLiteral(DEFAULT_PROJECT_TRIGGER)));
    m_groupTrigger->setText(grp.readEntry(CONFIG_GROUP_TRIGGER, QStringLiteral(DEFAULT_GROUP_TRIGGER)));
    m_codeCommand->setText(grp.readEntry(CONFIG_CODE_COMMAND, QStringLiteral(DEFAULT_CODE_COMMAND)));
    m_chromeCommand->setText(grp.readEntry(CONFIG_CHROME_COMMAND, QStringLiteral(DEFAULT_CHROME_COMMAND)));
    m_profileDir->setText(grp.readEntry(CONFIG_CHROME_PROFILE, QString()));
    m_projectTrigger->setEnabled(m_useTrigger->isChecked());
    m_groupTrigger->setEnabled(m_useTrigger->isChecked());
    m_projects = SavedGroups::readProjects(grp);
    refreshGroups();
    fillTable();
    setNeedsSave(false);
}

void ProjectsConfig::save()
{
    KCModule::save();
    KConfigGroup grp = runnerConfig();
    grp.writeEntry(CONFIG_USE_TRIGGER, m_useTrigger->isChecked());
    grp.writeEntry(CONFIG_SHOW_GROUPS, m_showGroups->isChecked());
    grp.writeEntry(CONFIG_PROJECT_TRIGGER, m_projectTrigger->text().trimmed());
    grp.writeEntry(CONFIG_GROUP_TRIGGER, m_groupTrigger->text().trimmed());
    grp.writeEntry(CONFIG_CODE_COMMAND, m_codeCommand->text().trimmed());
    grp.writeEntry(CONFIG_CHROME_COMMAND, m_chromeCommand->text().trimmed());
    grp.writeEntry(CONFIG_CHROME_PROFILE, m_profileDir->text().trimmed());
    SavedGroups::writeProjects(grp, m_projects);
    grp.sync();
    setNeedsSave(false);
}

void ProjectsConfig::defaults()
{
    KCModule::defaults();
    m_useTrigger->setChecked(true);
    m_showGroups->setChecked(true);
    m_projectTrigger->setText(QStringLiteral(DEFAULT_PROJECT_TRIGGER));
    m_groupTrigger->setText(QStringLiteral(DEFAULT_GROUP_TRIGGER));
    m_codeCommand->setText(QStringLiteral(DEFAULT_CODE_COMMAND));
    m_chromeCommand->setText(QStringLiteral(DEFAULT_CHROME_COMMAND));
    m_profileDir->clear();
    setNeedsSave(true);
}

void ProjectsConfig::fillTable()
{
    m_table->setRowCount(0);
    for (const Project &p : std::as_const(m_projects)) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *name = new QTableWidgetItem(QIcon::fromTheme(p.icon.isEmpty() ? u"folder-development"_s : p.icon), p.name);
        auto *path = new QTableWidgetItem(QDir::toNativeSeparators(p.path).replace(QDir::homePath(), u"~"_s));
        QString groupText = p.groupTitle;
        QString groupIcon = u"google-chrome"_s;
        bool found = false;
        for (const SavedGroup &g : std::as_const(m_groups)) {
            if (g.guid == p.groupGuid) {
                groupText = g.title.isEmpty() ? i18n("(untitled group)") : g.title;
                groupIcon = SavedGroups::colorIcon(g.color);
                found = true;
                break;
            }
        }
        if (!p.groupGuid.isEmpty() && !found) {
            groupText = i18n("%1 (missing)", groupText.isEmpty() ? p.groupGuid.left(8) : groupText);
            groupIcon = u"dialog-warning"_s;
        }
        auto *group = new QTableWidgetItem(p.groupGuid.isEmpty() ? QIcon() : QIcon::fromTheme(groupIcon), p.groupGuid.isEmpty() ? u"—"_s : groupText);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, path);
        m_table->setItem(row, 2, group);
    }
    updateButtons();
}

void ProjectsConfig::updateButtons()
{
    const bool has = !m_table->selectedItems().isEmpty();
    m_editButton->setEnabled(has);
    m_removeButton->setEnabled(has);
}

void ProjectsConfig::addProject()
{
    Project p;
    if (editProjectDialog(p, true)) {
        m_projects << p;
        std::sort(m_projects.begin(), m_projects.end(), [](const Project &a, const Project &b) {
            return a.name.localeAwareCompare(b.name) < 0;
        });
        fillTable();
        markAsChanged();
    }
}

void ProjectsConfig::editProject()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_projects.size()) {
        return;
    }
    Project p = m_projects.at(row);
    if (editProjectDialog(p, false)) {
        m_projects[row] = p;
        fillTable();
        m_table->selectRow(row);
        markAsChanged();
    }
}

void ProjectsConfig::removeProject()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_projects.size()) {
        return;
    }
    m_projects.removeAt(row);
    fillTable();
    markAsChanged();
}

bool ProjectsConfig::editProjectDialog(Project &project, bool isNew)
{
    QDialog dialog(widget());
    dialog.setWindowTitle(isNew ? i18n("Add project") : i18n("Edit project"));
    dialog.setMinimumWidth(520);
    auto *form = new QFormLayout(&dialog);

    auto *name = new QLineEdit(project.name, &dialog);
    name->setPlaceholderText(i18n("e.g. VDB Transit"));

    auto *pathRow = new QWidget(&dialog);
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto *path = new QLineEdit(project.path, pathRow);
    path->setPlaceholderText(i18n("folder or .code-workspace file (optional)"));
    auto *browseDir = new QPushButton(QIcon::fromTheme(u"folder-open"_s), i18n("Folder…"), pathRow);
    auto *browseWs = new QPushButton(QIcon::fromTheme(u"document-open"_s), i18n("Workspace…"), pathRow);
    pathLayout->addWidget(path);
    pathLayout->addWidget(browseDir);
    pathLayout->addWidget(browseWs);
    connect(browseDir, &QPushButton::clicked, &dialog, [&]() {
        const QString d = QFileDialog::getExistingDirectory(&dialog, i18n("Choose the project folder"), path->text().isEmpty() ? QDir::homePath() : path->text());
        if (!d.isEmpty()) {
            path->setText(d);
            if (name->text().isEmpty()) {
                name->setText(QDir(d).dirName());
            }
        }
    });
    connect(browseWs, &QPushButton::clicked, &dialog, [&]() {
        const QString f = QFileDialog::getOpenFileName(&dialog, i18n("Choose a VS Code workspace"), path->text().isEmpty() ? QDir::homePath() : path->text(), i18n("VS Code workspaces (*.code-workspace)"));
        if (!f.isEmpty()) {
            path->setText(f);
            if (name->text().isEmpty()) {
                name->setText(QFileInfo(f).completeBaseName());
            }
        }
    });

    auto *group = new QComboBox(&dialog);
    group->addItem(i18n("(no Chrome group)"), QString());
    int selected = 0;
    for (const SavedGroup &g : std::as_const(m_groups)) {
        const QString title = g.title.isEmpty() ? i18n("(untitled group)") : g.title;
        group->addItem(QIcon::fromTheme(SavedGroups::colorIcon(g.color)), i18np("%1 (%2 tab)", "%1 (%2 tabs)", title, g.tabCount), g.guid);
        if (g.guid == project.groupGuid) {
            selected = group->count() - 1;
        }
    }
    if (!project.groupGuid.isEmpty() && selected == 0) {
        group->addItem(QIcon::fromTheme(u"dialog-warning"_s), i18n("%1 (no longer in Chrome)", project.groupTitle), project.groupGuid);
        selected = group->count() - 1;
    }
    group->setCurrentIndex(selected);

    auto *icon = new QLineEdit(project.icon, &dialog);
    icon->setPlaceholderText(i18n("icon name, e.g. folder-development (optional)"));

    form->addRow(i18n("Name:"), name);
    form->addRow(i18n("VS Code:"), pathRow);
    form->addRow(i18n("Chrome group:"), group);
    form->addRow(i18n("Icon:"), icon);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, i18n("Missing name"), i18n("Give the project a name."));
            return;
        }
        if (path->text().trimmed().isEmpty() && group->currentData().toString().isEmpty()) {
            QMessageBox::warning(&dialog, i18n("Nothing to open"), i18n("Pick a VS Code folder, a Chrome group, or both."));
            return;
        }
        for (int i = 0; i < m_projects.size(); ++i) {
            if (m_projects.at(i).name == name->text().trimmed() && (isNew || i != m_table->currentRow())) {
                QMessageBox::warning(&dialog, i18n("Duplicate name"), i18n("There is already a project called \"%1\".", name->text().trimmed()));
                return;
            }
        }
        dialog.accept();
    });
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    project.name = name->text().trimmed();
    project.path = path->text().trimmed();
    project.groupGuid = group->currentData().toString();
    project.groupTitle = project.groupGuid.isEmpty() ? QString() : group->currentText().section(u" ("_s, 0, -2);
    if (project.groupTitle.isEmpty() && !project.groupGuid.isEmpty()) {
        project.groupTitle = group->currentText();
    }
    project.icon = icon->text().trimmed();
    return true;
}

#include "projectsconfig.moc"
