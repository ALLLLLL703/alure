add_library(alure_services STATIC Service.cpp Service.h NiriService.cpp NiriService.h CommandServices.cpp CommandServices.h
  DBusServices.cpp DBusServices.h TrayService.cpp TrayService.h NotificationService.cpp NotificationService.h Services.cpp Services.h)
target_link_libraries(alure_services PUBLIC alure_core Qt6::DBus Qt6::Network)
target_include_directories(alure_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
