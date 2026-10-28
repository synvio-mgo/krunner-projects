# Developer helpers, not installed.
#   runnertest <plugin.so> "<query>"...   runs queries through RunnerManager and prints matches
#   kcmshot <kcm.so> <out.png>            loads the config module offscreen and screenshots it
add_executable(runnertest runnertest.cpp)
target_link_libraries(runnertest Qt::Core Qt::Gui KF6::Runner KF6::ConfigCore KF6::CoreAddons)

add_executable(kcmshot kcmshot.cpp)
target_link_libraries(kcmshot Qt::Widgets KF6::KCMUtils KF6::CoreAddons)
