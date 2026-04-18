# Unity Test Framework
set(UNITY_SOURCES unity/unity.c)
set(UNITY_HEADERS unity/unity.h unity/unity_internals.h)

# Test sources
set(TEST_SOURCES
    test_fixture.c
    main.c
    test_uobject.c
    test_ucache.c
    test_uhash.c
)

# Create test executable
add_executable(uobject_tests
    ${UNITY_SOURCES}
    ${TEST_SOURCES}
)

target_include_directories(uobject_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/unity
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(uobject_tests PRIVATE uobject)

# Enable testing
add_test(NAME UnityTestSuite COMMAND uobject_tests)
