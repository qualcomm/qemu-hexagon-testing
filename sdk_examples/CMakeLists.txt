cmake_minimum_required(VERSION 3.16)
project(HexagonHVXExamples C ASM)

# Configuration options
option(BUILD_REFERENCE "Build reference executables" OFF)
option(BUILD_TESTS "Build test executables" ON)

# Set default values for toolchain variables if not already set
if(NOT HEXAGON_ARCH)
    set(HEXAGON_ARCH "v68" CACHE STRING "Hexagon DSP version")
endif()
if(NOT HVX_LENGTH)
    set(HVX_LENGTH "128" CACHE STRING "HVX vector length in bytes (64 or 128)")
endif()
add_compile_options(-mhvx -mhvx-length=${HVX_LENGTH}b)
add_link_options(-mhvx)

# SDK version for install paths
set(SDK_VERSION "6.3.0.0" CACHE STRING "Hexagon SDK version")

# Determine install subdirectory based on build type and system
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux builds (opensource toolchain) don't use SDK version
    if(BUILD_REFERENCE)
        set(INSTALL_SUBDIR "HVX_${HEXAGON_ARCH}_linux_c_ref")
    else()
        set(INSTALL_SUBDIR "HVX_${HEXAGON_ARCH}_linux")
    endif()
else()
    # Standalone OS builds use SDK version
    if(BUILD_REFERENCE)
        set(INSTALL_SUBDIR "HVX_${SDK_VERSION}_${HEXAGON_ARCH}_c_ref")
    else()
        set(INSTALL_SUBDIR "HVX_${SDK_VERSION}_${HEXAGON_ARCH}_asm")
    endif()
endif()

# Validate HVX_LENGTH and convert to LOG2VLEN for compatibility
if(HVX_LENGTH EQUAL 64)
    set(LOG2VLEN 6)
elseif(HVX_LENGTH EQUAL 128)
    set(LOG2VLEN 7)
else()
    message(FATAL_ERROR "HVX_LENGTH must be 64 or 128")
endif()

# Add architecture-specific flags to all targets
if(HEXAGON_ARCH)
    add_compile_options(-m${HEXAGON_ARCH})
    add_link_options(-m${HEXAGON_ARCH})
endif()

# Common include directories from SDK
set(HVX_EXAMPLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Hexagon_SDK/6.3.0.0/tools/HEXAGON_Tools/8.8.06/Examples/HVX)

# Add stub headers for Linux builds BEFORE SDK headers
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/include)
endif()

include_directories(${HVX_EXAMPLES_DIR}/common/include)

# Add toolchain-specific includes
if(DEFINED HEXAGON_COMMON_INCLUDES)
    include_directories(${HEXAGON_COMMON_INCLUDES})
endif()

# Programs that can build with both Standalone OS and Linux toolchains
# These programs use basic HVX intrinsics and don't rely on Standalone OS specific features
set(HVX_PROGRAMS_BOTH_TOOLCHAINS
    bilateral
    boxfilter
    conv3x3a16
    conv3x3a32
    dilate3x3
    epsilon
    gaussian
    histogram
    integrate
    invsqrt
    median
    ncc
    reciprocal
    sigma3x3
    sobel
)

# Programs that only work with Standalone OS toolchain
# These may use semihosting, special memory layouts, or SDK-specific features
set(HVX_PROGRAMS_STANDALONE_ONLY
    fast9
    harriscorner
    mipi2raw16
    nv12torgb8888
    wiener9x9
)

# Determine which programs to build based on the toolchain
message(STATUS "Building for ${CMAKE_SYSTEM_NAME}")
if(CMAKE_SYSTEM_NAME STREQUAL "StandaloneOS")
    # Standalone OS can build all programs
    set(HVX_PROGRAMS ${HVX_PROGRAMS_BOTH_TOOLCHAINS} ${HVX_PROGRAMS_STANDALONE_ONLY})

    # Build common subsystem library (may need SDK features)
    add_library(subsys STATIC ${HVX_EXAMPLES_DIR}/common/src/subsys.c)
    # Add include directories for subsys
    target_include_directories(subsys PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/include)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux can only build the compatible subset
    set(HVX_PROGRAMS ${HVX_PROGRAMS_BOTH_TOOLCHAINS})

    # Use subsys_stub.c for Linux builds
    add_library(subsys STATIC ${CMAKE_CURRENT_SOURCE_DIR}/stubs/src/subsys_stub.c)
else()
	message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}. Use StandaloneOS or Linux.")
endif()

# Function to build an HVX example program
function(add_hvx_example PROGRAM_NAME)
    # Set the path to HVX examples in the Hexagon SDK
    set(PROGRAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Hexagon_SDK/6.3.0.0/tools/HEXAGON_Tools/8.8.06/Examples/HVX/${PROGRAM_NAME})

    # Program-specific include directory
    include_directories(${PROGRAM_DIR}/include)

    # Determine the correct C wrapper file
    if(${PROGRAM_NAME} STREQUAL "epsilon")
        set(C_WRAPPER_NAME "sigma9x9")
    elseif(${PROGRAM_NAME} STREQUAL "harriscorner")
        set(C_WRAPPER_NAME "harris")
    elseif(${PROGRAM_NAME} STREQUAL "median")
        set(C_WRAPPER_NAME "median3x3")
    else()
        set(C_WRAPPER_NAME ${PROGRAM_NAME})
    endif()

    # Find specific assembly source files for each program
    if(${PROGRAM_NAME} STREQUAL "boxfilter")
        set(ASM_SOURCES ${PROGRAM_DIR}/asm_src/boxfilter.S)
    elseif(${PROGRAM_NAME} STREQUAL "gaussian")
        set(ASM_SOURCES
            ${PROGRAM_DIR}/asm_src/gaussian3x3.S
            ${PROGRAM_DIR}/asm_src/gaussian5x5.S
            ${PROGRAM_DIR}/asm_src/gaussian7x7.S
        )
    elseif(${PROGRAM_NAME} STREQUAL "harriscorner")
        set(ASM_SOURCES
            ${PROGRAM_DIR}/asm_src/gradient.S
            ${PROGRAM_DIR}/asm_src/blur5x5.S
            ${PROGRAM_DIR}/asm_src/response.S
            ${PROGRAM_DIR}/asm_src/search.S
            ${PROGRAM_DIR}/asm_src/getxpos.S
        )
    else()
        # For most programs, use the program name as the assembly file
        set(ASM_SOURCES ${PROGRAM_DIR}/asm_src/${C_WRAPPER_NAME}.S)
    endif()

    # Build the main executable (test program)
    # Some programs don't use the _a.c file, only test + assembly
    if(${PROGRAM_NAME} STREQUAL "invsqrt" OR ${PROGRAM_NAME} STREQUAL "reciprocal")
        add_executable(${PROGRAM_NAME}
            ${PROGRAM_DIR}/test/test_${C_WRAPPER_NAME}.c
            ${ASM_SOURCES}
        )
    else()
        add_executable(${PROGRAM_NAME}
            ${PROGRAM_DIR}/test/test_${C_WRAPPER_NAME}.c
            ${PROGRAM_DIR}/src/${C_WRAPPER_NAME}_a.c
            ${ASM_SOURCES}
        )
    endif()

    # Set Linux-specific properties
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Add stub headers with higher priority for Linux builds
        target_include_directories(${PROGRAM_NAME} BEFORE PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/stubs/include
        )

        # Linux linking - minimal dependencies
        target_link_libraries(${PROGRAM_NAME}
            subsys
            m  # math library
        )
        # Add static linking to avoid runtime dependencies
        target_link_options(${PROGRAM_NAME} PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "StandaloneOS")
        # Standalone OS linking
        target_link_libraries(${PROGRAM_NAME}
            subsys
            hexagon
        )
    endif()

    set_target_properties(${PROGRAM_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        OUTPUT_NAME ${PROGRAM_NAME}.exe
    )

    # Install the executable
    install(TARGETS ${PROGRAM_NAME}
        RUNTIME DESTINATION ${INSTALL_SUBDIR}/bin
    )

    # Build optimized version (library only, for performance comparison)
    if(BUILD_TESTS)
        if(${PROGRAM_NAME} STREQUAL "invsqrt" OR ${PROGRAM_NAME} STREQUAL "reciprocal")
            add_library(lib_${PROGRAM_NAME} STATIC
                ${ASM_SOURCES}
            )
        else()
            add_library(lib_${PROGRAM_NAME} STATIC
                ${PROGRAM_DIR}/src/${C_WRAPPER_NAME}_a.c
                ${ASM_SOURCES}
            )
        endif()

        set_target_properties(lib_${PROGRAM_NAME} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
        )
    endif()

    # Build reference executable if requested
    if(BUILD_REFERENCE)
        add_executable(ref_${PROGRAM_NAME}
            ${PROGRAM_DIR}/src/${C_WRAPPER_NAME}_c.c
            ${PROGRAM_DIR}/test/test_${C_WRAPPER_NAME}.c
        )

        # Reference builds use native compiler
        set_target_properties(ref_${PROGRAM_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ref
            OUTPUT_NAME ref_${PROGRAM_NAME}.exe
            C_COMPILER gcc
            COMPILE_FLAGS "-Wall -O3"
            LINK_FLAGS "-lm"
        )

        # Override compiler for reference builds
        target_compile_options(ref_${PROGRAM_NAME} PRIVATE -Wall -O3)
    endif()
endfunction()

# Build all HVX examples
foreach(PROGRAM ${HVX_PROGRAMS})
    add_hvx_example(${PROGRAM})
endforeach()

# Add custom target to run all tests
add_custom_target(run_tests
    COMMENT "Running all HVX example tests"
)

# Add custom target to run a subset of working tests
add_custom_target(run_basic_tests
    COMMENT "Running basic HVX example tests"
)

# Test vectors path
set(TESTVECTORS_DIR ${HVX_EXAMPLES_DIR}/testvectors)

# Add test commands for each program
foreach(PROGRAM ${HVX_PROGRAMS})
    add_custom_command(TARGET run_tests POST_BUILD
        COMMAND echo "Testing ${PROGRAM}..."
        COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_BINARY_DIR}/bin/${PROGRAM}.exe 1920 1080 ${TESTVECTORS_DIR}/football1920x1080.bin ${CMAKE_BINARY_DIR}/bin/${PROGRAM}_out.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endforeach()

# Add test commands for basic working programs
set(BASIC_TEST_PROGRAMS gaussian sobel bilateral boxfilter)
foreach(PROGRAM ${BASIC_TEST_PROGRAMS})
    add_custom_command(TARGET run_basic_tests POST_BUILD
        COMMAND echo "Testing ${PROGRAM}..."
        COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} ${CMAKE_BINARY_DIR}/bin/${PROGRAM}.exe 1920 1080 ${TESTVECTORS_DIR}/football1920x1080.bin ${CMAKE_BINARY_DIR}/bin/${PROGRAM}_out.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        DEPENDS ${PROGRAM}
    )
endforeach()

# Enable testing and add CTest integration
enable_testing()

# Add individual tests that can be run with ctest
foreach(PROGRAM ${BASIC_TEST_PROGRAMS})
    add_test(NAME ${PROGRAM}_test
        COMMAND ${PROGRAM}.exe 1920 1080 ${TESTVECTORS_DIR}/football1920x1080.bin ${CMAKE_BINARY_DIR}/bin/${PROGRAM}_ctest_out.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
    # Set dependencies so the program is built before testing
    add_dependencies(${PROGRAM} subsys)
endforeach()

# Install test vectors
install(DIRECTORY ${TESTVECTORS_DIR}/
    DESTINATION ${INSTALL_SUBDIR}/share/testvectors
    FILES_MATCHING PATTERN "*.bin"
)

# Install golden outputs
foreach(PROGRAM ${HVX_PROGRAMS})
    set(GOLDEN_FILE ${HVX_EXAMPLES_DIR}/${PROGRAM}/golden_out.bin)
    if(EXISTS ${GOLDEN_FILE})
        install(FILES ${GOLDEN_FILE}
            DESTINATION ${INSTALL_SUBDIR}/share/golden_outputs
            RENAME ${PROGRAM}_golden.bin
        )
    endif()
endforeach()

# Create a README for the installed package
if(BUILD_REFERENCE)
    set(BUILD_TYPE "C Reference Implementation")
else()
    set(BUILD_TYPE "Optimized Assembly Implementation")
endif()

# Create program list for README
set(PROGRAM_LIST "")
foreach(PROGRAM ${HVX_PROGRAMS})
    set(PROGRAM_LIST "${PROGRAM_LIST}\n- ${PROGRAM}")
endforeach()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/install_readme.md.in
    ${CMAKE_CURRENT_BINARY_DIR}/README.md
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/README.md
    DESTINATION ${INSTALL_SUBDIR}
)

# Print configuration summary
message(STATUS "")
message(STATUS "Hexagon HVX Examples Configuration:")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  SDK Version: ${SDK_VERSION}")
message(STATUS "  Hexagon Version: ${HEXAGON_ARCH}")
message(STATUS "  Vector Length: ${HVX_LENGTH}B (LOG2VLEN=${LOG2VLEN})")
message(STATUS "  Build Reference: ${BUILD_REFERENCE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Install Directory: ${INSTALL_SUBDIR}")
message(STATUS "")
message(STATUS "Programs to build:")
foreach(PROGRAM ${HVX_PROGRAMS})
    message(STATUS "  - ${PROGRAM}")
endforeach()
message(STATUS "")
