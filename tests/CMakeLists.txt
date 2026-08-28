# Johona core unit tests (Qt Test + CTest).
#
# Each test_*.cpp is a self-contained Qt Test binary linking johona-core.

# The tz table the tests use (mirrors the installed layout, so the tests
# work even when the source tree is not readable from the test environment).
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/share/johona")
configure_file("${CMAKE_SOURCE_DIR}/resources/tz_coordinates.json"
               "${CMAKE_BINARY_DIR}/share/johona/tz_coordinates.json" COPYONLY)

function(johona_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name}
        PRIVATE
            johona-core
            Qt6::Core
            Qt6::Test
    )
    target_compile_definitions(${name} PRIVATE
        JOHONA_BUILD_DIR="${CMAKE_BINARY_DIR}"
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

johona_add_test(test_suncalc)
johona_add_test(test_solar)
johona_add_test(test_config)
johona_add_test(test_themes)
johona_add_test(test_shuffle)
johona_add_test(test_scheduler)
johona_add_test(test_engine)
johona_add_test(test_location)
johona_add_test(test_migration)
