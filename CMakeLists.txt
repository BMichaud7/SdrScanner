cmake_minimum_required(VERSION 3.16)
project(SdrScanner VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED fftw3f)

# Proton pkg-config is broken on Ubuntu 24.04 — find libs directly
find_path(PROTON_INCLUDE_DIRS proton/connection.hpp REQUIRED)
find_library(PROTON_CPP_LIB   NAMES qpid-proton-cpp REQUIRED)
find_library(PROTON_C_LIB     NAMES qpid-proton     REQUIRED)
set(PROTON_LIBRARIES ${PROTON_CPP_LIB} ${PROTON_C_LIB})

include(FetchContent)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(json)

add_executable(sdr_scanner
    src/main.cpp
    src/MainWindow.cpp
    src/ScanWorker.cpp
    src/AmqpSession.cpp
    src/Spectrum.cpp
)
target_include_directories(sdr_scanner PRIVATE
    ${PROTON_INCLUDE_DIRS}
    ${FFTW3F_INCLUDE_DIRS}
    src/
)
target_link_libraries(sdr_scanner PRIVATE
    Qt6::Core Qt6::Widgets
    ${PROTON_LIBRARIES}
    ${FFTW3F_LIBRARIES}
    nlohmann_json::nlohmann_json
)
install(TARGETS sdr_scanner RUNTIME DESTINATION bin)
