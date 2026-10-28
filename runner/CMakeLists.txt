add_definitions(-DTRANSLATION_DOMAIN=\"krunner_projects\")
add_compile_definitions(KRUNNER_PROJECTS_DATADIR="${KRUNNER_PROJECTS_DATADIR}")

# ---- shared helper (saved group listing, config keys) ----
add_library(krunner_projects_common STATIC savedgroups.cpp savedgroups.h config_keys.h)
target_link_libraries(krunner_projects_common PUBLIC Qt::Core KF6::ConfigCore)
set_property(TARGET krunner_projects_common PROPERTY POSITION_INDEPENDENT_CODE ON)

# ---- the runner ----
kcoreaddons_add_plugin(krunner_projects
    SOURCES projectsrunner.cpp projectsrunner.h chromelauncher.cpp chromelauncher.h
    INSTALL_NAMESPACE "kf6/krunner")
target_link_libraries(krunner_projects
    krunner_projects_common
    Qt::DBus
    KF6::Runner
    KF6::I18n
    KF6::ConfigCore)

# ---- its configuration module (System Settings > KRunner > configure) ----
kcoreaddons_add_plugin(kcm_krunner_projects
    SOURCES projectsconfig.cpp projectsconfig.h
    INSTALL_NAMESPACE "kf6/krunner/kcms")
target_compile_definitions(kcm_krunner_projects PRIVATE -DKRUNNER_PLUGIN_NAME="krunner_projects")
target_link_libraries(kcm_krunner_projects
    krunner_projects_common
    Qt::Widgets
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::I18n
    KF6::ConfigCore)
