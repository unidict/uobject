cmake_minimum_required(VERSION 3.20)
project(libuobject VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- Options ----------
option(UOBJECT_BUILD_TESTS "Build tests" ON)
option(BUILD_SHARED_LIBS "Build shared library" OFF)

# ---------- Library ----------
file(GLOB UOBJECT_SOURCES src/*.c)
file(GLOB UOBJECT_HEADERS src/*.h)

set(UOBJECT_PUBLIC_HEADERS
    src/uobject.h
    src/ucache.h
    src/uhash.h
)

add_library(uobject ${UOBJECT_SOURCES})
target_include_directories(uobject
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(uobject PUBLIC pthread)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# ---------- Tests ----------
if(UOBJECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------- Install ----------
include(GNUInstallDirs)

install(TARGETS uobject
    EXPORT uobjectTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${UOBJECT_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/uobject)

install(EXPORT uobjectTargets
    FILE uobjectTargets.cmake
    NAMESPACE uobject::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/uobject
)
