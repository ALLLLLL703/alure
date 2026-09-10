add_library(alure_core STATIC ConfigEditing.cpp ConfigStore.cpp ConfigStore.h IconProvider.cpp IconProvider.h)
target_link_libraries(alure_core PUBLIC Qt6::Core Qt6::Gui Qt6::Quick PRIVATE tomlplusplus::tomlplusplus)
target_include_directories(alure_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_resources(alure_core defaults PREFIX / BASE ${PROJECT_SOURCE_DIR} FILES ${PROJECT_SOURCE_DIR}/config/default.toml ${PROJECT_SOURCE_DIR}/config/themes.toml)
