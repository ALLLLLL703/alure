add_library(alure_platform STATIC PanelHost.cpp PanelHost.h PopupPlacement.cpp PopupPlacement.h ClipboardHost.cpp ClipboardHost.h)
target_link_libraries(alure_platform PUBLIC alure_core Qt6::Quick LayerShellQt::Interface)
target_include_directories(alure_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
