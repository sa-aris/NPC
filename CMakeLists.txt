cmake_minimum_required(VERSION 3.16)
project(Aithena
    VERSION 1.0.0
    DESCRIPTION "C++17 NPC AI framework — FSM, behavior trees, emotions, memory, pathfinding, relationships, Lua scripting"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ─── NPC Library (static) ────────────────────────────────────────────

add_library(npc_lib STATIC
    src/npc.cpp
    src/world/world.cpp
)

target_include_directories(npc_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Required so npc_lib can be linked into the C API shared library
set_property(TARGET npc_lib PROPERTY POSITION_INDEPENDENT_CODE ON)

# ─── Village Simulation Demo ─────────────────────────────────────────

add_executable(village_sim examples/village_sim.cpp)
target_link_libraries(village_sim PRIVATE npc_lib)

# ─── Test Suite ──────────────────────────────────────────────────────

add_executable(run_tests tests/run_tests.cpp)
target_include_directories(run_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(run_tests PRIVATE npc_lib)
target_compile_options(run_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>
)

# CTest integration (optional: cmake --build build && ctest)
enable_testing()
add_test(NAME npc_tests COMMAND run_tests)

# ─── Lua Scripting Bridge (optional) ─────────────────────────────────
# Requires Lua 5.4 headers + library. On Ubuntu: sudo apt install liblua5.4-dev
# Disable with: cmake -DNPC_LUA_BRIDGE=OFF

option(NPC_LUA_BRIDGE "Build Lua scripting bridge and lua_village demo" ON)

if(NPC_LUA_BRIDGE AND NOT EMSCRIPTEN)
    find_package(Lua 5.4 QUIET)

    if(NOT LUA_FOUND)
        # Try pkg-config as fallback (handles lua5.4 package name on Debian/Ubuntu)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LUA QUIET lua5.4)
            if(LUA_FOUND)
                set(LUA_INCLUDE_DIR ${LUA_INCLUDE_DIRS})
                set(LUA_LIBRARIES   ${LUA_LINK_LIBRARIES})
            endif()
        endif()
    endif()

    if(LUA_FOUND)
        message(STATUS "Lua found: ${LUA_INCLUDE_DIR}")

        add_library(npc_lua STATIC src/lua_bridge.cpp)
        target_include_directories(npc_lua PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${LUA_INCLUDE_DIR}
        )
        target_link_libraries(npc_lua PUBLIC npc_lib ${LUA_LIBRARIES})
        target_compile_definitions(npc_lua PUBLIC NPC_HAS_LUA=1)

        add_executable(lua_village examples/lua_village.cpp)
        target_link_libraries(lua_village PRIVATE npc_lua)
        # Pass script directory so the binary can find the .lua files
        target_compile_definitions(lua_village PRIVATE
            NPC_SCRIPT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts"
        )

        add_test(NAME lua_village_smoke
                 COMMAND lua_village "${CMAKE_CURRENT_SOURCE_DIR}/examples/scripts")
    else()
        message(STATUS "Lua 5.4 not found — skipping npc_lua and lua_village."
                       " Install liblua5.4-dev to enable.")
    endif()
endif()

# ─── Performance Benchmarks (optional) ──────────────────────────────
# Build: cmake -B build -DNPC_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
#        cmake --build build --target run_benchmarks
#        ./build/run_benchmarks [--quick] [--csv]

option(NPC_BENCHMARKS "Build performance benchmark suite" OFF)

if(NPC_BENCHMARKS)
    add_executable(run_benchmarks benchmarks/run_benchmarks.cpp)
    target_include_directories(run_benchmarks PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(run_benchmarks PRIVATE npc_lib)
    target_compile_options(run_benchmarks PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native>
    )
endif()

# ─── C API shared library (optional, for Unity/Unreal/Godot) ─────────
# Build: cmake -B build -DNPC_SHARED=ON && cmake --build build --target npc_shared
# On Windows: links with NPC_BUILDING_DLL defined automatically.

option(NPC_SHARED "Build C API shared library (npc_shared)" OFF)

if(NPC_SHARED AND NOT EMSCRIPTEN)
    add_library(npc_shared SHARED src/npc_capi.cpp)
    target_include_directories(npc_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(npc_shared PRIVATE npc_lib)
    target_compile_definitions(npc_shared PRIVATE NPC_BUILDING_DLL)
    set_target_properties(npc_shared PROPERTIES
        OUTPUT_NAME "npc_shared"
        # Hide all symbols by default; only NPC_API ones are exported
        C_VISIBILITY_PRESET   hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

# ─── WASM target (Emscripten only) ───────────────────────────────────
# Build: emcmake cmake -B build-wasm && cmake --build build-wasm --target npc_wasm

if(EMSCRIPTEN)
    add_executable(npc_wasm src/wasm_api.cpp)
    target_include_directories(npc_wasm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(npc_wasm PRIVATE npc_lib)
    target_compile_options(npc_wasm PRIVATE -O2)
    target_link_options(npc_wasm PRIVATE
        "-sEXPORTED_FUNCTIONS=[_npc_init,_npc_step,_npc_is_complete,_malloc,_free]"
        "-sEXPORTED_RUNTIME_METHODS=[UTF8ToString]"
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=NpcModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=33554432"
        "-O2"
    )
    set_target_properties(npc_wasm PROPERTIES
        OUTPUT_NAME "npc_wasm"
        SUFFIX ".js"
    )
endif()
