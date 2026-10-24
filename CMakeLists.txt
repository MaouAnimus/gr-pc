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
    engine/src/kernel/event_flag.cpp
    engine/src/timing.cpp
    engine/src/physics/fixed_step.cpp
    engine/src/physics/gravity_motion.cpp
    engine/src/kernel/equeue.cpp
    engine/src/kernel/pthread_sync.cpp
    engine/src/kernel/pthread.cpp
    engine/src/kernel/sema.cpp
    engine/src/kernel/file.cpp
    engine/src/kernel/rwlock.cpp
    engine/src/kernel/direct_memory.cpp
    engine/src/kernel/native_tls.cpp
    engine/src/fiber/fiber.cpp
    engine/src/loader/elf.cpp
    engine/src/loader/tls_fixups.cpp
    engine/src/loader/reloc.cpp
    engine/src/loader/abi_thunk.cpp
    engine/src/fs/psarc.cpp
    engine/src/fs/fs.cpp
    engine/src/fs/fios.cpp
    engine/src/pad/pad.cpp
    engine/src/audio/audio.cpp
    engine/src/gnm/gnm.cpp
    engine/src/gnm/pm4.cpp
    engine/src/gnm/command_queue.cpp
    engine/src/gnm/submission.cpp
    engine/src/video.cpp
    engine/src/system_service.cpp
    engine/src/playgo.cpp
    engine/src/runner/runner.cpp
    engine/src/savedata/savedata.cpp
    engine/src/font/font.cpp
    engine/src/gnm/shader_pipeline.cpp
    engine/src/gnm/gcn_decoder.cpp
    engine/src/gnm/spirv_builder.cpp
    engine/src/gnm/gcn_translator.cpp
    engine/src/gnm/shader_cache.cpp
    engine/src/gnm/texture_decoder.cpp
    engine/src/libc/libc.cpp
    engine/src/avplayer/avplayer.cpp
    engine/src/avplayer/mf_decoder.cpp
    engine/src/avplayer/video_texture.cpp
    engine/src/rtc/rtc.cpp
    engine/src/audio/ngs2.cpp
    engine/src/audio/audio3d.cpp
    engine/src/registry/adapter_registry.cpp)
target_include_directories(gravity_runtime PUBLIC engine/include C:/msys64/clang64/include/freetype2)
target_compile_features(gravity_runtime PUBLIC cxx_std_23)
target_compile_definitions(gravity_runtime PUBLIC
    WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
target_compile_options(gravity_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(gravity_runtime PUBLIC z ole32 SDL3 vulkan-1 bcrypt freetype mfplat mfreadwrite mfuuid propsys uuid)

add_executable(gravity_runtime_probe engine/src/runtime_probe.cpp)
target_link_libraries(gravity_runtime_probe PRIVATE gravity_runtime)

# Launcher executable
add_executable(launcher launcher.cpp)
target_link_libraries(launcher PRIVATE gravity_runtime)
target_compile_options(launcher PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_options(gravity_runtime_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
    add_executable(gcn_decoder_tests tests/gcn_decoder_tests.cpp)
    target_link_libraries(gcn_decoder_tests PRIVATE gravity_runtime)
    target_compile_options(gcn_decoder_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite scalar literals formats branches invalid limits)
        add_test(NAME gcn_decoder_${suite} COMMAND gcn_decoder_tests ${suite})
        set_tests_properties(gcn_decoder_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(gcn_translator_tests tests/gcn_translator_tests.cpp)
    target_link_libraries(gcn_translator_tests PRIVATE gravity_runtime)
    target_compile_options(gcn_translator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite builder_empty builder_types builder_const builder_instr translate_vs translate_ps translate_alu translate_scalar translate_tex spirv_valid translate_invalid)
        add_test(NAME gcn_translator_${suite} COMMAND gcn_translator_tests ${suite})
        set_tests_properties(gcn_translator_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(shader_cache_tests tests/shader_cache_tests.cpp)
    target_link_libraries(shader_cache_tests PRIVATE gravity_runtime)
    target_compile_options(shader_cache_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite roundtrip miss identity corruption truncation concurrent invalid failures)
        add_test(NAME shader_cache_${suite} COMMAND shader_cache_tests ${suite})
        set_tests_properties(shader_cache_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(texture_decoder_tests tests/texture_decoder_tests.cpp)
    target_link_libraries(texture_decoder_tests PRIVATE gravity_runtime)
    target_compile_options(texture_decoder_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite morton format linear detile bc1 bc3 surface invalid)
        add_test(NAME texture_decoder_${suite} COMMAND texture_decoder_tests ${suite})
        set_tests_properties(texture_decoder_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(gravity_motion_tests tests/gravity_motion_tests.cpp)
    target_link_libraries(gravity_motion_tests PRIVATE gravity_runtime)
    target_compile_options(gravity_motion_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite free_fall tunneling orientation sliding features invalid)
        add_test(NAME gravity_motion_${suite} COMMAND gravity_motion_tests ${suite})
        set_tests_properties(gravity_motion_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(physics_runtime_tests tests/physics_runtime_tests.cpp)
    target_link_libraries(physics_runtime_tests PRIVATE gravity_runtime)
    target_compile_options(physics_runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite clocks deadlines fixed jobs cadence invalid)
        add_test(NAME physics_runtime_${suite} COMMAND physics_runtime_tests ${suite})
        set_tests_properties(physics_runtime_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(direct_memory_tests tests/direct_memory_tests.cpp)
    target_link_libraries(direct_memory_tests PRIVATE gravity_runtime)
    target_compile_options(direct_memory_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite mapping bounds)
        add_test(NAME direct_memory_${suite} COMMAND direct_memory_tests ${suite})
    endforeach()
    add_executable(rwlock_tests tests/rwlock_tests.cpp)
    target_link_libraries(rwlock_tests PRIVATE gravity_runtime)
    target_compile_options(rwlock_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite concurrent ownership)
        add_test(NAME rwlock_${suite} COMMAND rwlock_tests ${suite})
        set_tests_properties(rwlock_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(sema_tests tests/sema_tests.cpp)
    target_link_libraries(sema_tests PRIVATE gravity_runtime)
    target_compile_options(sema_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite count wait cancel invalid)
        add_test(NAME sema_${suite} COMMAND sema_tests ${suite})
        set_tests_properties(sema_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(pthread_tests tests/pthread_tests.cpp)
    target_link_libraries(pthread_tests PRIVATE gravity_runtime)
    target_compile_options(pthread_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite callback attributes invalid)
        add_test(NAME pthread_${suite} COMMAND pthread_tests ${suite})
        set_tests_properties(pthread_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(native_tls_tests tests/native_tls_tests.cpp)
    target_link_libraries(native_tls_tests PRIVATE gravity_runtime)
    target_compile_options(native_tls_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite layout concurrent invalid)
        add_test(NAME native_tls_${suite} COMMAND native_tls_tests ${suite})
        set_tests_properties(native_tls_${suite} PROPERTIES TIMEOUT 10 SKIP_RETURN_CODE 77)
    endforeach()
    add_executable(pthread_sync_tests tests/pthread_sync_tests.cpp)
    target_link_libraries(pthread_sync_tests PRIVATE gravity_runtime)
    target_compile_options(pthread_sync_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite attributes recursive concurrent invalid cond cond_handoff)
        add_test(NAME pthread_sync_${suite} COMMAND pthread_sync_tests ${suite})
        set_tests_properties(pthread_sync_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(event_flag_tests tests/event_flag_tests.cpp)
    target_link_libraries(event_flag_tests PRIVATE gravity_runtime)
    target_compile_options(event_flag_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle bits wait cancel concurrent invalid registry)
        add_test(NAME event_flag_${suite} COMMAND event_flag_tests ${suite})
        set_tests_properties(event_flag_${suite} PROPERTIES TIMEOUT 15)
    endforeach()
    add_executable(equeue_tests tests/equeue_tests.cpp)
    target_link_libraries(equeue_tests PRIVATE gravity_runtime)
    target_compile_options(equeue_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle wait invalid)
        add_test(NAME equeue_${suite} COMMAND equeue_tests ${suite})
        set_tests_properties(equeue_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(system_service_tests tests/system_service_tests.cpp)
    target_link_libraries(system_service_tests PRIVATE gravity_runtime)
    target_compile_options(system_service_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite parameters invalid host)
        add_test(NAME system_service_${suite} COMMAND system_service_tests ${suite})
    endforeach()
    add_executable(fios_tests tests/fios_tests.cpp)
    target_link_libraries(fios_tests PRIVATE gravity_runtime)
    target_compile_options(fios_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle invalid registry filters mount file_ops)
        add_test(NAME fios_${suite} COMMAND fios_tests ${suite})
        set_tests_properties(fios_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(playgo_tests tests/playgo_tests.cpp)
    target_link_libraries(playgo_tests PRIVATE gravity_runtime)
    target_compile_options(playgo_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle locus registry)
        add_test(NAME playgo_${suite} COMMAND playgo_tests ${suite})
        set_tests_properties(playgo_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    add_executable(libc_tests tests/libc_tests.cpp)
    target_link_libraries(libc_tests PRIVATE gravity_runtime)
    target_compile_options(libc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite math string_memory conversions cxx_runtime mspace atomics_tables setjmp_longjmp)
        add_test(NAME libc_${suite} COMMAND libc_tests ${suite})
        set_tests_properties(libc_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
    find_program(GLSLC glslc HINTS "C:/msys64/clang64/bin" REQUIRED)
    foreach(stage vert frag)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.${stage}.spv
            COMMAND ${GLSLC} ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/pm4_triangle.${stage}
                    -o ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.${stage}.spv
            DEPENDS tests/shaders/pm4_triangle.${stage} VERBATIM)
    endforeach()
    add_executable(pm4_tests tests/pm4_tests.cpp tests/pm4_vulkan_test.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.frag.spv)
    target_link_libraries(pm4_tests PRIVATE gravity_runtime)
    target_compile_options(pm4_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite decode invalid indirect queue concurrency adapter vulkan)
        add_test(NAME pm4_${suite} COMMAND pm4_tests ${suite})
        set_tests_properties(pm4_${suite} PROPERTIES TIMEOUT 30)
    endforeach()
    set_tests_properties(pm4_vulkan PROPERTIES SKIP_RETURN_CODE 77)
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
    foreach(suite success read_fault write_fault div_zero timeout module_mapping large_stack)
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

    # SaveData & User Service tests
    add_executable(savedata_tests tests/savedata_tests.cpp)
    target_link_libraries(savedata_tests PRIVATE gravity_runtime)
    target_compile_options(savedata_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite user_service save_mount_vfs save_dialog)
        add_test(NAME savedata_${suite} COMMAND savedata_tests ${suite})
        set_tests_properties(savedata_${suite} PROPERTIES TIMEOUT 10)
    endforeach()

    # Font & FreeType tests
    add_executable(font_tests tests/font_tests.cpp)
    target_link_libraries(font_tests PRIVATE gravity_runtime)
    target_compile_options(font_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle metrics_rendering)
        add_test(NAME font_${suite} COMMAND font_tests ${suite})
        set_tests_properties(font_${suite} PROPERTIES TIMEOUT 10)
    endforeach()

    # RTC tests
    add_executable(rtc_tests tests/rtc_tests.cpp)
    target_link_libraries(rtc_tests PRIVATE gravity_runtime)
    target_compile_options(rtc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite ticks rfc3339 datetime)
        add_test(NAME rtc_${suite} COMMAND rtc_tests ${suite})
        set_tests_properties(rtc_${suite} PROPERTIES TIMEOUT 10)
    endforeach()

    # AvPlayer tests
    add_executable(avplayer_tests tests/avplayer_tests.cpp tests/avplayer_fixture.cpp tests/pm4_vulkan_test.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.vert.spv
        ${CMAKE_CURRENT_BINARY_DIR}/pm4_triangle.frag.spv)
    target_link_libraries(avplayer_tests PRIVATE gravity_runtime)
    target_compile_options(avplayer_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite lifecycle playback invalid looping psarc performance vulkan)
        add_test(NAME avplayer_${suite} COMMAND avplayer_tests ${suite})
        set_tests_properties(avplayer_${suite} PROPERTIES TIMEOUT 60)
    endforeach()
    set_tests_properties(avplayer_vulkan PROPERTIES SKIP_RETURN_CODE 77)

    # NGS2 & Audio3D tests
    add_executable(audio_advanced_tests tests/audio_advanced_tests.cpp)
    target_link_libraries(audio_advanced_tests PRIVATE gravity_runtime)
    target_compile_options(audio_advanced_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    foreach(suite ngs2 audio3d integration)
        add_test(NAME audio_advanced_${suite} COMMAND audio_advanced_tests ${suite})
        set_tests_properties(audio_advanced_${suite} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

