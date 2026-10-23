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
    engine/src/video.cpp)
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
endif()
