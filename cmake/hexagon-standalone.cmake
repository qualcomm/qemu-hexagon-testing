# Hexagon Standalone OS Toolchain File
# This toolchain uses the Hexagon SDK's hexagon-clang for Standalone OS (baremetal with semihosting)

set(CMAKE_SYSTEM_NAME StandaloneOS)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

# Hexagon SDK paths
if(NOT HEXAGON_SDK_ROOT)
    set(HEXAGON_SDK_ROOT ${CMAKE_CURRENT_LIST_DIR}/../Hexagon_SDK/6.3.0.0)
endif()
set(HEXAGON_TOOLS_ROOT ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/8.8.06/Tools)

# Toolchain executables
set(CMAKE_C_COMPILER ${HEXAGON_TOOLS_ROOT}/bin/hexagon-clang)
set(CMAKE_CXX_COMPILER ${HEXAGON_TOOLS_ROOT}/bin/hexagon-clang++)
set(CMAKE_ASM_COMPILER ${HEXAGON_TOOLS_ROOT}/bin/hexagon-clang)

# Find root path for the target environment
set(CMAKE_FIND_ROOT_PATH ${HEXAGON_TOOLS_ROOT}/target/hexagon)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Hexagon-specific variables
set(HEXAGON_ARCH "v68" CACHE STRING "Hexagon architecture version")
set(LOG2VLEN "7" CACHE STRING "Log2 of vector length (6 for 64B, 7 for 128B)")
set(PREBUILT_LIB_DIR "hexagon_toolv88_v${HEXAGON_ARCH}")
set(HEXAGON_LIB_DIR ${HEXAGON_TOOLS_ROOT}/target/hexagon/lib)

# HVX compiler flags detection
execute_process(
    COMMAND ${CMAKE_C_COMPILER} --help
    OUTPUT_VARIABLE COMPILER_HELP
    ERROR_QUIET
)

# Set HVX flags based on vector length
if(LOG2VLEN EQUAL 7)
    if(COMPILER_HELP MATCHES "mhvx-length")
        set(MHVX_FLAGS "-mhvx -mhvx-length=128B")
    else()
        set(MHVX_FLAGS "-mhvx -mhvx-double")
    endif()
else()
    if(COMPILER_HELP MATCHES "mhvx-length")
        set(MHVX_FLAGS "-mhvx -mhvx-length=64B")
    else()
        set(MHVX_FLAGS "-mhvx")
    endif()
endif()

# Common compiler flags for Standalone OS (based on QEMU build flags)
set(CMAKE_C_FLAGS_INIT "-m${HEXAGON_ARCH} -G0 -O2 ${MHVX_FLAGS} -DLOG2VLEN=${LOG2VLEN} -DQDSP6SS_PUB_BASE=0xFE200000 -DCSR_BASE=0xfc900000 -DQTMR_FREQ=19200000 -DIRQ1=3 -DIRQ2=4")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fexceptions")
set(CMAKE_ASM_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m${HEXAGON_ARCH}")

# Common include directories
set(HEXAGON_COMMON_INCLUDES
    ${HEXAGON_SDK_ROOT}/incs
    ${HEXAGON_SDK_ROOT}/incs/stddef
    ${HEXAGON_TOOLS_ROOT}/target/hexagon/include
)

# Variables for the Hexagon SDK build system
set(OS_TYPE "StandaloneOS")
set(V ${PREBUILT_LIB_DIR})
set(BUILD_NAME ${PREBUILT_LIB_DIR})
set(DSP_VERSION ${HEXAGON_ARCH})

# Set variable to indicate this is the Standalone OS toolchain
set(HEXAGON_STANDALONE_TOOLCHAIN TRUE)
