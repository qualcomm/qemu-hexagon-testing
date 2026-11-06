cmake_minimum_required(VERSION 3.16)
# The tests provide their own entry point and do not use the SDK runtime.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(HexagonPacketTests C ASM)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "StandaloneOS")
    message(FATAL_ERROR "Packet tests require cmake/hexagon-standalone.cmake")
endif()
if(NOT HEXAGON_ARCH STREQUAL "v85")
    message(FATAL_ERROR "Packet tests require -DHEXAGON_ARCH=v85")
endif()

include(CheckCCompilerFlag)
check_c_compiler_flag("-mv85 -mhmx -mhlx" HEXAGON_PACKET_ISA_SUPPORTED)
if(NOT HEXAGON_PACKET_ISA_SUPPORTED)
    message(FATAL_ERROR "Packet tests require a compiler with V85, HMX and HLX support")
endif()

set(SDK_VERSION "6.7.0.0" CACHE STRING "Hexagon SDK version")
set(INSTALL_SUBDIR "PacketTests_${SDK_VERSION}_${HEXAGON_ARCH}")
add_compile_options(-ffreestanding -fno-builtin -fno-stack-protector)
add_link_options(-nostdlib -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/packet.ld)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
enable_testing()
set(QEMU_HEXAGON "" CACHE FILEPATH "qemu-system-hexagon to test")
set(PACKET_PROGRAMS "")

function(add_packet_test name)
    add_executable(${name} ${name}.c start.S common.c)
    set(PACKET_PROGRAMS ${PACKET_PROGRAMS} ${name} PARENT_SCOPE)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
                 ${CMAKE_CURRENT_SOURCE_DIR}/packet.ld)
    if(QEMU_HEXAGON)
        add_test(NAME ${name} COMMAND ${QEMU_HEXAGON} -M V85NA_1
                 -display none -kernel $<TARGET_FILE:${name}>)
        set_tests_properties(${name} PROPERTIES TIMEOUT 30
                             PASS_REGULAR_EXPRESSION "PASS ${name}"
                             FAIL_REGULAR_EXPRESSION "FAIL")
    endif()
    install(TARGETS ${name} RUNTIME DESTINATION ${INSTALL_SUBDIR}/bin)
endfunction()

add_packet_test(pb01_predicate_read)
add_packet_test(pb02_usr_mask)
add_packet_test(pb03_call_priority)
add_packet_test(pb04_hmx_address)
target_compile_options(pb04_hmx_address PRIVATE -mhmx)
add_packet_test(pb05_hlx_disable)
target_compile_options(pb05_hlx_disable PRIVATE -mhlx)
add_packet_test(pb06_fetch_cause)
add_packet_test(pb07_fetch_pc)
add_packet_test(pb08_fault_slot)

# Match the reproducible HLX archive layout used by QEMU functional tests.
set(PACKET_PACKAGE_FILES "")
foreach(PROGRAM ${PACKET_PROGRAMS})
    list(APPEND PACKET_PACKAGE_FILES bin/${PROGRAM})
endforeach()
find_program(GZIP_EXECUTABLE gzip)
if(NOT GZIP_EXECUTABLE)
    message(FATAL_ERROR "gzip is required to package the packet tests")
endif()
add_custom_target(package_packet_tests
    COMMAND ${CMAKE_COMMAND} -E tar cf
        ${CMAKE_BINARY_DIR}/packet_tests.tar
        --format=gnutar "--mtime=1970-01-01 00:00:00 UTC"
        ${PACKET_PACKAGE_FILES}
    COMMAND ${GZIP_EXECUTABLE} -n -f ${CMAKE_BINARY_DIR}/packet_tests.tar
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${PACKET_PROGRAMS}
    COMMENT "Packaging packet regression tests"
    VERBATIM
)
install(FILES README.md DESTINATION ${INSTALL_SUBDIR})
