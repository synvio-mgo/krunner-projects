cmake_minimum_required(VERSION 3.16)

project(krunner-projects VERSION 0.1.0)

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core Gui Widgets DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Runner I18n Config CoreAddons KCMUtils)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Python helpers (saved tab group reader + native messaging host)
set(KRUNNER_PROJECTS_DATADIR "${KDE_INSTALL_FULL_DATADIR}/krunner-projects")
install(PROGRAMS host/chromegroups.py host/krunner_projects_host.py DESTINATION ${KRUNNER_PROJECTS_DATADIR})
install(FILES host/be.synvio.krunner_projects.json.in DESTINATION ${KRUNNER_PROJECTS_DATADIR})
install(DIRECTORY extension/ DESTINATION ${KRUNNER_PROJECTS_DATADIR}/extension)
install(FILES keys/extension_id.txt DESTINATION ${KRUNNER_PROJECTS_DATADIR})

add_subdirectory(runner)

option(BUILD_DEV_TOOLS "Build the runnertest/kcmshot developer helpers" OFF)
if(BUILD_DEV_TOOLS)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
