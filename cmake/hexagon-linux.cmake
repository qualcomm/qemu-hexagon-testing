# Hexagon Linux Toolchain File
# This toolchain uses the hexagon-linux-musl-clang for Linux userspace

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

# Toolchain executables - assume they are in PATH
set(CMAKE_C_COMPILER hexagon-linux-musl-clang)
set(CMAKE_CXX_COMPILER hexagon-linux-musl-clang++)
set(CMAKE_ASM_COMPILER hexagon-linux-musl-clang)
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-hexagon)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Hexagon-specific variables
set(HEXAGON_ARCH "v68" CACHE STRING "Hexagon architecture version")
set(LOG2VLEN "7" CACHE STRING "Log2 of vector length (6 for 64B, 7 for 128B)")

# HVX compiler flags detection
execute_process(
    COMMAND ${CMAKE_C_COMPILER} --help
    OUTPUT_VARIABLE COMPILER_HELP
    ERROR_QUIET
)

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m${HEXAGON_ARCH} -static")

# Common include directories will be handled by the compiler's default search paths

# Variables for distinguishing from Standalone OS builds
set(OS_TYPE "HLOS")

# Set variable to indicate this is the Linux toolchain
set(HEXAGON_LINUX_TOOLCHAIN TRUE)
