cmake_minimum_required(VERSION 2.8)
project(libbiji)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBTC REQUIRED IMPORTED_TARGET libbitcoin-client)

add_definitions(-fconcepts -std=c++2a)

include_directories(${LIBBTC_INCLUDE_DIRS})
link_directories(${LIBBTC_LIBRARY_DIRS})
add_library(biji SHARED src/biji.cpp src/get_history.cpp)

target_include_directories(biji PRIVATE include)

set(PKG_CONFIG_FILES ${CMAKE_BINARY_DIR}/biji.pc)
configure_file(${CMAKE_SOURCE_DIR}/biji.pc.in ${CMAKE_BINARY_DIR}/biji.pc @ONLY)
list(APPEND PKG_CONFIG_FILES ${CMAKE_BINARY_DIR}/biji.pc)
install(FILES ${PKG_CONFIG_FILES} DESTINATION lib${LIB_SUFFIX}/pkgconfig)

install(TARGETS biji DESTINATION lib)
install(FILES include/biji.hpp DESTINATION include)

