cmake_minimum_required(VERSION 3.24)
project(KardsFactionToolkit VERSION 0.2.0 LANGUAGES C CXX ASM_MASM)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Use Windows x64 with the MSVC C++ toolchain (-A x64).")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(KARDS_GAME_EXE "" CACHE FILEPATH "Path to a locally owned, supported game EXE")
set(KARDS_FACTION_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/factions.json" CACHE FILEPATH "Faction configuration JSON")
set(KARDS_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/profiles/kards-20261002-win64.json" CACHE FILEPATH "Verified game version profile")
if(NOT EXISTS "${KARDS_GAME_EXE}")
  message(FATAL_ERROR "Set -DKARDS_GAME_EXE=<path to 1.exe or shipping EXE>.")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import pefile"
  RESULT_VARIABLE PEFILE_RESULT OUTPUT_QUIET ERROR_QUIET)
if(NOT PEFILE_RESULT EQUAL 0)
  message(FATAL_ERROR "Install Python dependencies: python -m pip install -r requirements.txt")
endif()
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_FILES)
foreach(NAME profile.h factions.h dwm_exports.h dwmapi.def forwards.asm manifest.json)
  list(APPEND GENERATED_FILES "${GENERATED_DIR}/${NAME}")
endforeach()
add_custom_command(OUTPUT ${GENERATED_FILES}
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_profile.py"
    --game-exe "${KARDS_GAME_EXE}" --config "${KARDS_FACTION_CONFIG}"
    --profile "${KARDS_BUILD_PROFILE}" --output-dir "${GENERATED_DIR}"
  DEPENDS tools/generate_profile.py "${KARDS_GAME_EXE}" "${KARDS_FACTION_CONFIG}"
    "${KARDS_BUILD_PROFILE}" "$ENV{SystemRoot}/System32/dwmapi.dll"
  VERBATIM COMMENT "Validating game build and generating faction profile")
add_custom_target(generate_profile DEPENDS ${GENERATED_FILES})

add_library(minhook STATIC vendor/minhook/src/buffer.c vendor/minhook/src/hook.c
  vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c)
target_compile_definitions(minhook PRIVATE _WIN32_WINNT=0x0A00)
add_library(faction_core STATIC src/core.cpp)
add_dependencies(faction_core generate_profile)
target_include_directories(faction_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(faction_core PUBLIC NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(faction_core PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(faction_core PUBLIC minhook)

add_library(dwmapi SHARED src/proxy.cpp "${GENERATED_DIR}/forwards.asm" "${GENERATED_DIR}/dwmapi.def")
add_dependencies(dwmapi generate_profile)
target_link_libraries(dwmapi PRIVATE faction_core)
target_compile_options(dwmapi PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/utf-8;/permissive->")
target_link_options(dwmapi PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)

include(CTest)
if(BUILD_TESTING)
  add_executable(native_tests tests/native_tests.cpp)
  target_link_libraries(native_tests PRIVATE faction_core)
  target_compile_options(native_tests PRIVATE /W4 /utf-8 /permissive-)
  add_executable(proxy_tests tests/proxy_tests.cpp)
  add_dependencies(proxy_tests dwmapi)
  target_include_directories(proxy_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  # The .lib suffix selects the Windows SDK import library, not our dwmapi target.
  # Visual Studio supplies the selected SDK library directories at build time.
  target_link_libraries(proxy_tests PRIVATE dwmapi.lib)
  target_compile_options(proxy_tests PRIVATE /W4 /utf-8 /permissive-)
  add_test(NAME config_tests COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests -p test_*.py)
  set_tests_properties(config_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME native_tests COMMAND native_tests "${KARDS_GAME_EXE}")
  add_test(NAME proxy_tests COMMAND proxy_tests)
endif()

install(TARGETS dwmapi RUNTIME DESTINATION .)
install(FILES "${GENERATED_DIR}/manifest.json" README.md README.en.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(FILES vendor/minhook/LICENSE.txt DESTINATION licenses RENAME MinHook.LICENSE.txt)
install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING PATTERN "*.md")
