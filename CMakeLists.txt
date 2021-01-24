cmake_minimum_required(VERSION 3.12.0 FATAL_ERROR)
project(proxy)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wconversion -Wsign-conversion)
add_compile_definitions($<$<CONFIG:Debug>:TRACE>)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -fsanitize=leak")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -fsanitize=leak")

add_executable(
    ${PROJECT_NAME}
    proxy.cpp
    fd.h fd.cpp
    pipe.h pipe.cpp
    unidirectional_fwd.h unidirectional_fwd.cpp
    bidirectional_connection.h bidirectional_connection.cpp
)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

configure_file(tox.ini.in tox.ini @ONLY)
