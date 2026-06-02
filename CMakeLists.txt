cmake_minimum_required(VERSION 3.16)
project(SdrScanner VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# ── FFTW3 ─────────────────────────────────────────────────────────────────────
# On Windows (vcpkg): FFTW3 ships a CMake config package.
# On Linux: use pkg-config (system packages or Homebrew).
if(WIN32)
    find_package(FFTW3 CONFIG REQUIRED)
    # vcpkg fftw3 exports FFTW3::fftw3 (double); use it for the GUI display path.
    if(TARGET FFTW3::fftw3f)
        set(FFTW3_LINK_LIBS FFTW3::fftw3f)
    else()
        set(FFTW3_LINK_LIBS FFTW3::fftw3)
    endif()
    set(FFTW3_INCS "")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFTW3F REQUIRED fftw3f)
    set(FFTW3_LINK_LIBS ${FFTW3F_LIBRARIES})
    set(FFTW3_INCS      ${FFTW3F_INCLUDE_DIRS})
endif()

# ── Proton ────────────────────────────────────────────────────────────────────
# pkg-config for proton is broken on Ubuntu 24.04 multiarch and absent on
# Windows — find headers and libs directly (works with vcpkg and system installs).
find_path(PROTON_INCLUDE_DIRS proton/connection.hpp REQUIRED)
find_library(PROTON_CPP_LIB   NAMES qpid-proton-cpp REQUIRED)
find_library(PROTON_C_LIB     NAMES qpid-proton     REQUIRED)
set(PROTON_LIBRARIES ${PROTON_CPP_LIB} ${PROTON_C_LIB})

# ── nlohmann/json (header-only, FetchContent on all platforms) ────────────────
include(FetchContent)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(json)

# ── Au units library (header-only) ───────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(Au
    GIT_REPOSITORY https://github.com/aurora-opensource/au
    GIT_TAG "0.5.1"
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(Au)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(sdr_scanner WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/ScanWorker.cpp
    src/AmqpSession.cpp
    src/Spectrum.cpp
)
target_include_directories(sdr_scanner PRIVATE
    ${PROTON_INCLUDE_DIRS}
    ${FFTW3_INCS}
    src/
)
target_link_libraries(sdr_scanner PRIVATE
    Qt6::Core Qt6::Widgets
    ${PROTON_LIBRARIES}
    ${FFTW3_LINK_LIBS}
    nlohmann_json::nlohmann_json
    Au::au
)
install(TARGETS sdr_scanner RUNTIME DESTINATION bin)
