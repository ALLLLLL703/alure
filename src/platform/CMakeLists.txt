add_library(alure_platform STATIC TrayLauncherHost.cpp TrayLauncherHost.h BackgroundBlur.cpp BackgroundBlur.h PanelHost.cpp PanelHost.h PopupPlacement.cpp PopupPlacement.h ClipboardHost.cpp ClipboardHost.h)
target_link_libraries(alure_platform PUBLIC alure_core Qt6::Quick LayerShellQt::Interface PRIVATE KF6::WindowSystem)
target_include_directories(alure_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
