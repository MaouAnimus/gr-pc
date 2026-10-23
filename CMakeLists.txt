cmake_minimum_required(VERSION 3.24)
project(GravityRushRuntime VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This runtime requires Windows x86-64.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Use Clang x86-64: the fiber entry contract uses sysv_abi.")
endif()

add_library(gravity_runtime STATIC
    engine/src/memory/address_space.cpp
    engine/src/kernel/kernel.cpp
    engine/src/fiber/fiber.cpp
    engine/src/loader/elf.cpp
    engine/src/loader/reloc.cpp
    engine/src/loader/abi_thunk.cpp
    engine/src/fs/psarc.cpp
    engine/src/fs/fs.cpp
    engine/src/pad/pad.cpp
    engine/src/audio/audio.cpp
    engine/src/gnm/gnm.cpp
    engine/src/video.cpp
    engine/src/runner/runner.cpp
    engine/src/registry/adapter_registry.cpp)
target_include_directories(gravity_runtime PUBLIC engine/include)
target_compile_features(gravity_runtime PUBLIC cxx_std_23)
target_compile_definitions(gravity_runtime PUBLIC
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(gravity_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(gravity_runtime PUBLIC z ole32 SDL3 vulkan-1)

add_executable(gravity_runtime_probe engine/src/runtime_probe.cpp)
target_link_libraries(gravity_runtime_probe PRIVATE gravity_runtime)

# Launcher executable
add_executable(launcher launcher.cpp)
target_link_libraries(launcher PRIVATE gravity_runtime)
target_compile_options(launcher PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_options(gravity_runtime_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
    add_executable(stabilization_tests tests/stabilization_tests.cpp)
    target_link_libraries(stabilization_tests PRIVATE gravity_runtime)
    target_compile_options(stabilization_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite loader relocations fs abi headless)
        add_test(NAME regression_${suite} COMMAND stabilization_tests ${suite})
        set_tests_properties(regression_${suite} PROPERTIES TIMEOUT 30)
    endforeach()

    add_executable(runtime_tests tests/runtime_tests.cpp)
    target_link_libraries(runtime_tests PRIVATE gravity_runtime)
    target_compile_options(runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite memory kernel fibers loader psarc pad audio gnm video)
        add_test(NAME runtime_${suite} COMMAND runtime_tests ${suite})
        set_tests_properties(runtime_${suite} PROPERTIES TIMEOUT 30)
    endforeach()
    set_tests_properties(runtime_audio runtime_video PROPERTIES SKIP_RETURN_CODE 77)

    # N04: Isolated Runner tests
    add_executable(runner_tests tests/runner_tests.cpp)
    target_link_libraries(runner_tests PRIVATE gravity_runtime)
    target_compile_options(runner_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite success read_fault write_fault div_zero timeout module_mapping)
        add_test(NAME runner_${suite} COMMAND runner_tests ${suite})
        set_tests_properties(runner_${suite} PROPERTIES TIMEOUT 10)
    endforeach()

    # N05: System V AMD64 ABI Matrix tests
    add_executable(abi_matrix_tests tests/abi_matrix_tests.cpp)
    target_link_libraries(abi_matrix_tests PRIVATE gravity_runtime)
    target_compile_options(abi_matrix_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite registers stack floats mixed structs callbacks varargs returns)
        add_test(NAME abi_matrix_${suite} COMMAND abi_matrix_tests ${suite})
        set_tests_properties(abi_matrix_${suite} PROPERTIES TIMEOUT 10)
    endforeach()

    # N06: Fiber Layout, TLS and Stack Isolation tests
    add_executable(fiber_tls_tests tests/fiber_tls_tests.cpp)
    target_link_libraries(fiber_tls_tests PRIVATE gravity_runtime)
    target_compile_options(fiber_tls_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite layout lifecycle stack tls concurrent)
        add_test(NAME fiber_tls_${suite} COMMAND fiber_tls_tests ${suite})
        set_tests_properties(fiber_tls_${suite} PROPERTIES TIMEOUT 15)
    endforeach()

    # N07: Typed Adapter Registry & Unresolved Traps
    add_executable(registry_tests tests/registry_tests.cpp)
    target_link_libraries(registry_tests PRIVATE gravity_runtime)
    target_compile_options(registry_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lookup execution trap)
        add_test(NAME registry_${suite} COMMAND registry_tests ${suite})
        set_tests_properties(registry_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
endif()
