# The johona GUI executable.
add_executable(johona
    main.cpp
    mainwindow.cpp
    themestab.cpp
    settstab.cpp
    schedulertab.cpp
    previewwidget.cpp
    schedulepreview.cpp
)
target_include_directories(johona PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(johona
    PRIVATE
        johona-core
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Svg
)
install(TARGETS johona RUNTIME DESTINATION bin)
