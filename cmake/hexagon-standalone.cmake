# Hexagon Standalone OS Toolchain File
# This toolchain uses the Hexagon SDK's hexagon-clang for Standalone OS (baremetal with semihosting)

set(CMAKE_SYSTEM_NAME StandaloneOS)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

# Hexagon SDK paths - check environment variable first, then CMake variable, then default
if(DEFINED ENV{HEXAGON_SDK_ROOT})
    set(HEXAGON_SDK_ROOT $ENV{HEXAGON_SDK_ROOT})
elseif(NOT HEXAGON_SDK_ROOT)
    set(HEXAGON_SDK_ROOT /opt/Hexagon_SDK/6.4.0.2)
endif()

# Determine tools root and toolchain paths
if(DEFINED ENV{HEXAGON_SDK_TOOLCHAIN})
    # Use the full toolchain path provided by CI
    set(HEXAGON_TOOLCHAIN_BIN $ENV{HEXAGON_SDK_TOOLCHAIN})
    get_filename_component(HEXAGON_TOOLS_ROOT ${HEXAGON_TOOLCHAIN_BIN} DIRECTORY)
elseif(DEFINED ENV{HEXAGON_TOOLS_ROOT})
    set(HEXAGON_TOOLS_ROOT $ENV{HEXAGON_TOOLS_ROOT})
    # If environment provides tools root, we need to append /Tools if it's not already there
    if(NOT HEXAGON_TOOLS_ROOT MATCHES "Tools$")
        set(HEXAGON_TOOLS_ROOT ${HEXAGON_TOOLS_ROOT}/Tools)
    endif()
    set(HEXAGON_TOOLCHAIN_BIN ${HEXAGON_TOOLS_ROOT}/bin)
else()
    # Auto-detect tools version by looking for available directories
    file(GLOB TOOLS_VERSIONS LIST_DIRECTORIES true "${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/*")
    if(TOOLS_VERSIONS)
        list(GET TOOLS_VERSIONS 0 FIRST_VERSION)
        get_filename_component(TOOLS_VERSION ${FIRST_VERSION} NAME)
        set(HEXAGON_TOOLS_ROOT ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/${TOOLS_VERSION}/Tools)
        set(HEXAGON_TOOLCHAIN_BIN ${HEXAGON_TOOLS_ROOT}/bin)
        message(STATUS "Auto-detected Hexagon tools version: ${TOOLS_VERSION}")
    else()
        # Fallback to known version
        set(HEXAGON_TOOLS_ROOT ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/19.0.04/Tools)
        set(HEXAGON_TOOLCHAIN_BIN ${HEXAGON_TOOLS_ROOT}/bin)
    endif()
endif()

message(STATUS "Using HEXAGON_SDK_ROOT: ${HEXAGON_SDK_ROOT}")
message(STATUS "Using HEXAGON_TOOLS_ROOT: ${HEXAGON_TOOLS_ROOT}")
message(STATUS "Using HEXAGON_TOOLCHAIN_BIN: ${HEXAGON_TOOLCHAIN_BIN}")

# Toolchain executables
set(CMAKE_C_COMPILER ${HEXAGON_TOOLCHAIN_BIN}/hexagon-clang)
set(CMAKE_CXX_COMPILER ${HEXAGON_TOOLCHAIN_BIN}/hexagon-clang++)
set(CMAKE_ASM_COMPILER ${HEXAGON_TOOLCHAIN_BIN}/hexagon-clang)

# Additional tools
set(CMAKE_AR ${HEXAGON_TOOLCHAIN_BIN}/hexagon-ar)
set(CMAKE_LINKER ${HEXAGON_TOOLCHAIN_BIN}/ld.qcld)
set(CMAKE_NM ${HEXAGON_TOOLCHAIN_BIN}/hexagon-nm)
set(CMAKE_OBJCOPY ${HEXAGON_TOOLCHAIN_BIN}/hexagon-llvm-objcopy)
set(CMAKE_OBJDUMP ${HEXAGON_TOOLCHAIN_BIN}/hexagon-llvm-objdump)
set(CMAKE_RANLIB ${HEXAGON_TOOLCHAIN_BIN}/hexagon-ranlib)
set(CMAKE_SIZE ${HEXAGON_TOOLCHAIN_BIN}/hexagon-size)
set(CMAKE_STRIP ${HEXAGON_TOOLCHAIN_BIN}/hexagon-strip)

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
