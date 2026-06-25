# CMake toolchain for cross-compiling a Windows x64 build from Linux with
# MinGW-w64. Produces a statically-linked VST3 (no extra runtime DLLs needed).
#
#   sudo apt-get install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=packaging/windows/mingw-w64-x86_64.toolchain.cmake
#
# Note: this is a convenience build. The official Windows binary is the MSVC
# build produced by the GitHub Actions "Windows Installer" workflow.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

# Use the POSIX threading variant so std::thread / std::mutex are available.
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Statically link the GCC/C++/pthread runtimes into the plugin so end users
# don't need any MinGW redistributable DLLs.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
