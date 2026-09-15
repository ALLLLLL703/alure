qt_add_executable(alure-trayctl main.cpp)
target_link_libraries(alure-trayctl PRIVATE alure_services)
install(TARGETS alure-trayctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
