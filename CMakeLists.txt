cmake_minimum_required(VERSION 3.16)
project(bello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automoc so Q_OBJECT is processed
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Network REQUIRED)

find_package(PkgConfig REQUIRED)

# Locate uuid headers if present (some distros provide uuid/uuid.h in different paths)
find_path(UUID_INCLUDE_DIR NAMES uuid/uuid.h
  PATHS /usr/include /usr/local/include /usr/include/uuid /usr/include/libuuid)
if(UUID_INCLUDE_DIR)
  message(STATUS "Found uuid headers in ${UUID_INCLUDE_DIR}")
  include_directories(${UUID_INCLUDE_DIR})
else()
  message(STATUS "uuid headers not found by CMake; if build fails, install libuuid-dev or uuid-dev")
endif()

# Allow user to pass prebuilt/include paths via CMake variables
if(DEFINED DUCKDB_INCLUDE_DIRS AND DEFINED DUCKDB_LIBRARIES)
  message(STATUS "Using user-specified DUCKDB_INCLUDE_DIRS=${DUCKDB_INCLUDE_DIRS} and DUCKDB_LIBRARIES=${DUCKDB_LIBRARIES}")
  set(DUCKDB_FOUND TRUE)
  set(DUCKDB_INCLUDE_DIR ${DUCKDB_INCLUDE_DIRS})
  set(DUCKDB_LIBRARY ${DUCKDB_LIBRARIES})
else()
  # Try to find duckdb via pkg-config
  pkg_check_modules(DUCKDB duckdb QUIET)
  if(NOT DUCKDB_FOUND)
    message(STATUS "duckdb pkg-config not found; trying to find library 'duckdb' manually")
    find_library(DUCKDB_LIBRARY NAMES duckdb libduckdb PATHS /usr/lib /usr/local/lib /usr/lib64 ${CMAKE_SOURCE_DIR}/duckdb)
    find_path(DUCKDB_INCLUDE_DIR NAMES duckdb.hpp PATHS /usr/include /usr/local/include ${CMAKE_SOURCE_DIR}/duckdb)
    if(NOT DUCKDB_LIBRARY OR NOT DUCKDB_INCLUDE_DIR)
      message(FATAL_ERROR "duckdb not found. Install duckdb dev package or set PKG_CONFIG_PATH, or pass DUCKDB_INCLUDE_DIRS and DUCKDB_LIBRARIES to cmake.")
    endif()
  else()
    set(DUCKDB_LIBRARY ${DUCKDB_LIBRARIES})
    set(DUCKDB_INCLUDE_DIR ${DUCKDB_INCLUDEDIR})
  endif()
endif()

include_directories(${DUCKDB_INCLUDE_DIR})
set(DUCKDB_LIBRARIES ${DUCKDB_LIBRARY})

set(SRC
  src/main.cpp
)

set(HEADERS
  src/MainWindow.h
  src/Database.h
)

qt_add_executable(bello ${SRC} ${HEADERS})

# Set up platform-specific libraries
if(WIN32)
    target_link_libraries(bello PRIVATE Qt6::Widgets Qt6::Network ${DUCKDB_LIBRARIES} ole32)
else()
    target_link_libraries(bello PRIVATE Qt6::Widgets Qt6::Network ${DUCKDB_LIBRARIES} uuid)
endif()

# Ensure the runtime linker finds the prebuilt duckdb next to the source tree
# so users don't need to set LD_LIBRARY_PATH. Use a relative rpath from the
# binary: $ORIGIN/../duckdb (binary in build/, duckdb in repo root bello-cpp/duckdb).
set_target_properties(bello PROPERTIES
  BUILD_RPATH "\$ORIGIN/../duckdb"
  INSTALL_RPATH "\$ORIGIN/../duckdb"
)

install(TARGETS bello RUNTIME DESTINATION bin)

# If an in-repo `duckdb/` folder exists (for example from the
# prebuilt `libduckdb-linux-amd64.zip`), copy it into the binary output
# directory after building so the produced `build/` tree is portable and
# the runtime linker can find `libduckdb.so` without setting
# `LD_LIBRARY_PATH` manually.
if(EXISTS "${CMAKE_SOURCE_DIR}/duckdb")
  add_custom_command(TARGET bello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bello>/duckdb
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/duckdb" $<TARGET_FILE_DIR:bello>/duckdb
    COMMENT "Copying in-repo duckdb to output for portable runs"
  )
endif()
