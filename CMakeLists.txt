cmake_minimum_required(VERSION 3.12)  # 3.12+ for better macOS support; compatible with 10.12 deployment

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
project("classyc" C)

# Set default build type
if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

#Set debug c flags
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -fno-omit-frame-pointer" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-g" CACHE STRING "")

# ------------------ Profile-Guided Optimization (PGO) support ----------------
# Usage:
#   1) cmake -B build -S . -DCLASSYC_PGO_GENERATE=ON && cmake --build build
#   2) Run representative workloads (e.g. examples via ./bin/classyc -eg).
#      Profile data (*.gcda) is emitted next to the objects or under CMAKE_BINARY_DIR.
#   3) cmake -B build -S . -DCLASSYC_PGO_USE=ON && cmake --build build
#      The second build consumes the profile and emits an optimized binary.
option(CLASSYC_PGO_GENERATE "Instrument for PGO (generate phase)" OFF)
option(CLASSYC_PGO_USE     "Use collected profile for PGO optimization" OFF)

if(CLASSYC_PGO_GENERATE AND CLASSYC_PGO_USE)
  message(FATAL_ERROR "CLASSYC_PGO_GENERATE and CLASSYC_PGO_USE are mutually exclusive")
endif()

if(CLASSYC_PGO_GENERATE)
  message(STATUS "PGO generate phase enabled (-fprofile-generate)")
  # For GCC: -fprofile-generate writes .gcda next to objects (or use -fprofile-dir=...)
  # We also enable -O2 so the generated profile is meaningful.
  add_compile_options(-fprofile-generate -O2)
  add_link_options(-fprofile-generate)
elseif(CLASSYC_PGO_USE)
  message(STATUS "PGO use phase enabled (-fprofile-use)")
  add_compile_options(-fprofile-use -fprofile-correction -O3)
  add_link_options(-fprofile-use)
endif()

# ------------------ MIR Subdirectory -----------------------
# Add MIR as a subdirectory (assumes ${MIRDIR}/CMakeLists.txt exists)
set(MIRDIR ext/mir)
# The top-level project owns the b2obj / b2objmac tools (built from ./src), so
# tell the vendored MIR not to build its own duplicate (same target name).
set(CLASSYC_TOPLEVEL_B2OBJ ON)
add_subdirectory(${MIRDIR})

# ------------------ classyc Compiler -----------------------
add_executable(classyc
    src/classyc.c
    src/classyc-driver.c
)

target_include_directories(classyc PRIVATE
    ${CMAKE_SOURCE_DIR}/${MIRDIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(classyc PRIVATE mir_static)

# Define NDEBUG for release-like builds (matches your manual command)
target_compile_definitions(classyc PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

# Platform-specific flags for compatibility with macOS 10.12 and Ubuntu 24.04
if(APPLE)
  # macOS 10.12 deployment target
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12" CACHE STRING "Minimum OS X deployment version" FORCE)

  # Use GNU11 or compatible C standard
  target_compile_options(classyc PRIVATE -std=gnu11)

  # Common warnings and optimizations
  target_compile_options(classyc PRIVATE
        -Wno-abi
        -fsigned-char
        -fPIC
    )

  # Link math library if needed (usually not on macOS)
else()
  # Linux (Ubuntu 24.04)
  target_compile_options(classyc PRIVATE
        -std=gnu11
        -Wno-abi
        -fsigned-char
        -fPIC
        $<$<CONFIG:RELEASE>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
    )

  target_link_libraries(classyc PRIVATE m)
endif()

# ------------------ classyc-lsp Language Server ------------------
# A second front end: reuses the compiler (src/classyc.c) but provides its own
# main (no codegen / exec).  Speaks LSP over stdio for editors (Zed / VS Code).
add_executable(classyc-lsp
    src/classyc.c
    src/classyc-lsp.c
)

target_include_directories(classyc-lsp PRIVATE
    ${CMAKE_SOURCE_DIR}/${MIRDIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(classyc-lsp PRIVATE mir_static)
target_compile_definitions(classyc-lsp PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

if(APPLE)
  target_compile_options(classyc-lsp PRIVATE -std=gnu11 -Wno-abi -fsigned-char -fPIC)
else()
  target_compile_options(classyc-lsp PRIVATE
        -std=gnu11
        -Wno-abi
        -fsigned-char
        -fPIC
        $<$<CONFIG:RELEASE>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
    )
  target_link_libraries(classyc-lsp PRIVATE m)
endif()

# ------------------ b2obj / b2objmac -----------------------
# b2obj (ELF) for Linux; b2objmac (Mach-O) for macOS
if( APPLE )
  add_executable(b2objmac
        src/b2objmac.c
    )
  target_include_directories(b2objmac PRIVATE ${CMAKE_SOURCE_DIR}/${MIRDIR})
  target_link_libraries(b2objmac PRIVATE mir_static ${CMAKE_DL_LIBS})

  # Create symlink or alias for convenience (b2obj -> b2objmac on macOS)
  add_custom_command(TARGET b2objmac POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE:b2objmac>
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/b2obj
        COMMENT "Creating b2obj symlink to b2objmac on macOS"
    )
elseif( UNIX )
  add_executable(b2obj
        src/b2obj.c
    )
  target_include_directories(b2obj PRIVATE ${CMAKE_SOURCE_DIR}/${MIRDIR})
  target_link_libraries(b2obj PRIVATE mir_static ${CMAKE_DL_LIBS})
endif()

# ------------------ Optional: Examples and classyc-aot support -----------------------
# You can extend this to build examples if desired, but since they use classyc-aot script,
# the main targets above should suffice for manual use.

# Install rules (optional)
install(TARGETS classyc RUNTIME DESTINATION bin)
if(APPLE)
  install(TARGETS b2objmac RUNTIME DESTINATION bin)
  install(FILES ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/b2obj DESTINATION bin)
elseif(UNIX)
  install(TARGETS b2obj RUNTIME DESTINATION bin)
endif()

message(STATUS "classyc build configured. MIR integrated as subdirectory.")
message(STATUS "Build with: cmake -B build -S . && cmake --build build")
message(STATUS "Output: bin/classyc and b2obj/b2objmac")
