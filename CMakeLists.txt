cmake_minimum_required(VERSION 4.0)
project(ixplorir-driver-suite LANGUAGES C)

# Installer собирается через llvm-mingw (кросс-компиляция с Linux на Windows)
# Драйвер (.sys) собирается отдельно на Windows через MSVC + WDK

add_subdirectory(installer)
