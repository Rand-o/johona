# johona-core — headless core library (solar, config, themes, backends,
# scheduler, engine, location, migration).

add_library(johona-core STATIC
    suncalc.cpp
    solar.cpp
    config.cpp
    themes.cpp
    shuffle.cpp
    backends.cpp
    scheduler.cpp
    engine.cpp
    location.cpp
    migration.cpp
    autostart.cpp
    dbusrelay.hpp   # Q_OBJECT header: listed so AUTOMOC generates its moc
    portalwaiter.hpp  # Q_OBJECT header: listed so AUTOMOC generates its moc
)

target_include_directories(johona-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(johona-core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        zip  # vendored libzip (static)
)

# Resources bundled with the core (tz → coordinates table).
set_target_properties(johona-core PROPERTIES
    AUTORCC ON
)
