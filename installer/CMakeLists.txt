cmake_minimum_required(VERSION 4.0)
project(ixplorir-installer LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(ixplorir-installer WIN32 main.c)

target_link_options(ixplorir-installer PRIVATE
    -static
    -static-libgcc
)

target_link_libraries(ixplorir-installer PRIVATE advapi32 winhttp crypt32)
