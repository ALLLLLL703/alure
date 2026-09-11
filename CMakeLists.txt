cmake_minimum_required(VERSION 3.24)
project(Alure VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Svg DBus Network)
find_package(LayerShellQt 6.6 REQUIRED)
find_package(tomlplusplus 3.4 REQUIRED)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/services)
qt_add_executable(alure src/app/main.cpp)
target_link_libraries(alure PRIVATE alure_core alure_platform alure_services Qt6::QuickControls2 Qt6::QuickDialogs2)
set(ALURE_UI_FILES
  qml/Panel.qml
  qml/PanelGroup.qml
  qml/Settings.qml
  qml/SettingField.qml
  qml/SettingsFields.js
  qml/Ui.js
  qml/ModuleStrip.qml
  qml/ShellButton.qml
  qml/ModulePopup.qml
  qml/MediaPopup.qml
  qml/MediaButton.qml
  qml/ClipboardPopup.qml
  qml/ClipboardOverlay.qml
  qml/TrayMenu.qml
  qml/Popup.qml
  qml/CalendarView.qml
  qml/InfoText.qml
  qml/Toast.qml
  assets/icons/settings.svg
  assets/icons/battery.svg
  assets/icons/media.svg
  assets/icons/clipboard.svg
  assets/icons/fallback.svg
  assets/icons/wifi.svg
  assets/icons/volume.svg
  assets/icons/workspaces.svg
  assets/icons/tray.svg
  assets/icons/notifications.svg
  assets/icons/bluetooth.svg
  assets/icons/alure.svg
  assets/icons/calendar.svg
  assets/icons/updates.svg
  assets/icons/refresh.svg
  assets/icons/close.svg
  assets/icons/previous.svg
  assets/icons/next.svg
  assets/icons/up.svg
  assets/icons/down.svg
  assets/icons/check.svg
  assets/icons/circle.svg
  assets/icons/minus.svg
)
qt_add_resources(alure ui PREFIX / FILES ${ALURE_UI_FILES})
install(TARGETS alure RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/default.toml config/multi-panel.toml config/services-example.toml config/clipboard-example.toml config/four-edge.toml config/layouts.toml DESTINATION ${CMAKE_INSTALL_DATADIR}/alure)
# systemd performs its own quoting, environment and specifier expansion.
set(ALURE_SYSTEMD_EXECUTABLE "${CMAKE_INSTALL_FULL_BINDIR}/alure")
if(ALURE_SYSTEMD_EXECUTABLE MATCHES "[\n\r]")
  message(FATAL_ERROR "Install path cannot contain newlines")
endif()
string(REPLACE "\\" "\\\\" ALURE_SYSTEMD_EXECUTABLE "${ALURE_SYSTEMD_EXECUTABLE}")
string(REPLACE "\"" "\\\"" ALURE_SYSTEMD_EXECUTABLE "${ALURE_SYSTEMD_EXECUTABLE}")
string(REPLACE "%" "%%" ALURE_SYSTEMD_EXECUTABLE "${ALURE_SYSTEMD_EXECUTABLE}")
string(REPLACE "$" "$$" ALURE_SYSTEMD_EXECUTABLE "${ALURE_SYSTEMD_EXECUTABLE}")
configure_file(packaging/alure.service.in alure.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/alure.service DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_subdirectory(tests)
endif()
