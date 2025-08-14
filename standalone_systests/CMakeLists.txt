cmake_minimum_required(VERSION 3.16)
project(HexagonStandaloneSysTests C ASM)

# Set default values for toolchain variables if not already set
if(NOT HEXAGON_ARCH)
    set(HEXAGON_ARCH "v68" CACHE STRING "Hexagon DSP version")
endif()

# SDK version for install paths
set(SDK_VERSION "6.3.0.0" CACHE STRING "Hexagon SDK version")

# Determine install subdirectory based on build type
set(INSTALL_SUBDIR "StandaloneSysTests_${SDK_VERSION}_${HEXAGON_ARCH}")

# Add architecture-specific flags to all targets
if(HEXAGON_ARCH)
    add_compile_options(-m${HEXAGON_ARCH})
    add_link_options(-m${HEXAGON_ARCH})
endif()

# Only build for Standalone OS toolchain
if(NOT CMAKE_SYSTEM_NAME STREQUAL "StandaloneOS")
    message(FATAL_ERROR "Standalone system tests only support StandaloneOS toolchain. Current system: ${CMAKE_SYSTEM_NAME}")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
# Add parent directory to include path for "../hex_test.h" references
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Add toolchain-specific includes
if(DEFINED HEXAGON_COMMON_INCLUDES)
    include_directories(${HEXAGON_COMMON_INCLUDES})
endif()

# Add SDK header paths
if(DEFINED HEXAGON_TOOLS_ROOT)
    include_directories(${HEXAGON_TOOLS_ROOT}/8.8.06/Tools/target/hexagon/include)
endif()

# Programs that should have main functions (standalone executables)
set(STANDALONE_PROGRAMS
    access
    badva
    bestwait
    checkforpriv
    ciad-siad
    dirent
    double_ex
    fastint
    fastl2vic
    float_excp
    fopen
    framelimit
    ftrunc
    getcwd
    gregs
    hvx_64b
    hvx_ext
    hvx-multi
    hvx_nocoproc
    inf-loop
    int_range
    invalid_insn_for_rev
    invalid_opcode
    k0lock-syscfg
    k0lock
    k0locklock
    levelint
    llsc_on_excp
    memcpy
    mmu_asids
    mmu_cacheops
    mmu_multi_tlb
    mmu_overlap
    mmu_page_size
    mmu_permissions
    multiple_writes
    pend_wake_wait
    pendalot
    pmu
    qfloat_test
    qtimer
    qtimer_test
    reg-reads
    reg-writes
    rev
    semihost
    # standalone_hw          # Disabled: missing test_set_prio function
    standalone_vec
    start
    swi
    swi2
    swi_fs
    swi_wait
    sys_atomics
    sys_reg_mut
    test-thread
    thread_scheduling
    timer_reg
    tlblock
    tlblocklock
    udma
    unaligned
    vid-group
    vid_reg
    vm_test
    vtcm_error
    # vwctrl                 # Disabled: missing Q6_mxmem2_bias_A intrinsic
)

# Support/utility source files that are not main programs
set(SUPPORT_SOURCES
    src/thread_common.c
    src/util.c
)

# Assembly files needed for some programs
set(COMMON_ASM_SOURCES
    src/boot.S
    src/dummy_mutex.S
    src/hvx-multi-asm.S
    src/monitor_insts.S
    src/simple-assign.S
    src/single_step_asm.S
    src/tlb-miss-tlblock.S
)

# Create a support library with common functionality
add_library(systest_support STATIC ${SUPPORT_SOURCES})
target_include_directories(systest_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Function to build a standalone system test
function(add_systest_program PROGRAM_NAME)
    # Check if source file exists
    set(SOURCE_FILE src/${PROGRAM_NAME}.c)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE})
        # Create executable
        add_executable(${PROGRAM_NAME} ${SOURCE_FILE})

        # Link with support library and system libraries
        target_link_libraries(${PROGRAM_NAME}
            systest_support
            hexagon
        )

        # Set output properties
        set_target_properties(${PROGRAM_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            OUTPUT_NAME ${PROGRAM_NAME}
        )

        # Install the executable
        install(TARGETS ${PROGRAM_NAME}
            RUNTIME DESTINATION ${INSTALL_SUBDIR}/bin
        )

        message(STATUS "Added system test: ${PROGRAM_NAME}")
    else()
        message(WARNING "Source file not found for program: ${PROGRAM_NAME} (${SOURCE_FILE})")
    endif()
endfunction()

# Build all standalone programs
foreach(PROGRAM ${STANDALONE_PROGRAMS})
    add_systest_program(${PROGRAM})
endforeach()

# Special handling for programs that need assembly files
# boot - needs special linker script handling (disabled - assembly-only, no main function)
# if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/boot.S)
#     add_executable(boot_test src/boot.S)
#     set_target_properties(boot_test PROPERTIES
#         RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
#         OUTPUT_NAME boot_test.exe
#     )
#     target_link_libraries(boot_test hexagon)
#     install(TARGETS boot_test RUNTIME DESTINATION ${INSTALL_SUBDIR}/bin)
# endif()

# Programs that might need special assembly support
set(SPECIAL_ASM_PROGRAMS
    single_step
    tlb-miss-tlblock
)

foreach(PROGRAM ${SPECIAL_ASM_PROGRAMS})
    # Try both naming conventions: program-asm.S and program_asm.S
    set(ASM_FILE_HYPHEN src/${PROGRAM}-asm.S)
    set(ASM_FILE_UNDERSCORE src/${PROGRAM}_asm.S)
    set(C_FILE src/${PROGRAM}.c)
    
    set(ASM_FILE "")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${ASM_FILE_HYPHEN})
        set(ASM_FILE ${ASM_FILE_HYPHEN})
    elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${ASM_FILE_UNDERSCORE})
        set(ASM_FILE ${ASM_FILE_UNDERSCORE})
    endif()
    
    if(ASM_FILE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${C_FILE})
        add_executable(${PROGRAM} ${C_FILE} ${ASM_FILE})
        target_link_libraries(${PROGRAM} systest_support hexagon)
        set_target_properties(${PROGRAM} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
            OUTPUT_NAME ${PROGRAM}
        )
        install(TARGETS ${PROGRAM} RUNTIME DESTINATION ${INSTALL_SUBDIR}/bin)
        message(STATUS "Added special assembly program: ${PROGRAM}")
    endif()
endforeach()

# Add custom target to run all tests
add_custom_target(run_systests
    COMMENT "Running all standalone system tests"
)

# Create a README for the installed package
set(BUILD_TYPE "Standalone System Tests")

# Create program list for README
set(PROGRAM_LIST "")
foreach(PROGRAM ${STANDALONE_PROGRAMS})
    set(PROGRAM_LIST "${PROGRAM_LIST}\n- ${PROGRAM}")
endforeach()

# Install additional files
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${INSTALL_SUBDIR}/include
    FILES_MATCHING PATTERN "*.h"
)

# Install any reference files or test data
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/expected-gregs-warnings.txt)
    install(FILES expected-gregs-warnings.txt
        DESTINATION ${INSTALL_SUBDIR}/share
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/qfloat.out.ref)
    install(FILES qfloat.out.ref
        DESTINATION ${INSTALL_SUBDIR}/share
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Hexagon Standalone System Tests Configuration:")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  SDK Version: ${SDK_VERSION}")
message(STATUS "  Hexagon Version: ${HEXAGON_ARCH}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Install Directory: ${INSTALL_SUBDIR}")
message(STATUS "")
message(STATUS "Programs to build:")
foreach(PROGRAM ${STANDALONE_PROGRAMS})
    set(SOURCE_FILE src/${PROGRAM}.c)
endforeach()
message(STATUS "")
