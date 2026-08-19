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
    src/cyfiber.c
)

target_include_directories(classyc PRIVATE
    ${CMAKE_SOURCE_DIR}/${MIRDIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ext/ccchan
)

target_link_libraries(classyc PRIVATE mir_static pthread)

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

  target_link_libraries(classyc PRIVATE m pthread)
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

# nmb: nm-like inspector for .bmir / .mir (symbols, traps, debug types).
add_executable(nmb src/nmb.c)
target_include_directories(nmb PRIVATE ${CMAKE_SOURCE_DIR}/${MIRDIR})
target_link_libraries(nmb PRIVATE mir_static)
if(NOT APPLE)
  target_link_libraries(nmb PRIVATE m)
endif()

# ------------------ Optional: Examples and classyc-aot support -----------------------
# You can extend this to build examples if desired, but since they use classyc-aot script,
# the main targets above should suffice for manual use.

# ------------------ Install ------------------------------------------------
# Works identically for a system install (default prefix /usr/local, needs
# sudo/root) and a per-user install (-DCMAKE_INSTALL_PREFIX=$HOME/.classyc,
# no elevated privileges) — same rules, different prefix.
include(GNUInstallDirs)

install(TARGETS classyc classyc-lsp nmb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS mir_static
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(APPLE)
  install(TARGETS b2objmac RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # b2obj is a convenience alias for b2objmac on macOS.  Create the symlink
  # fresh inside the install tree (relative target) at install time, rather
  # than installing the build-tree symlink verbatim — that one points back
  # into the build directory and breaks once it's removed.
  install(CODE "
    execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
        b2objmac
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/b2obj\")
  ")
elseif(UNIX)
  install(TARGETS b2obj RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# classyc-aot: the AOT driver script (classyc -> b2obj -> cc), installed
# alongside the binaries it drives.
install(PROGRAMS ${CMAKE_SOURCE_DIR}/classyc-aot.sh
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME classyc-aot
)

# Headers: the whole include/ tree (list.h, sqlite.h, dict.h, ..., the
# per-arch aarch64/ ppc64/ riscv64/ s390x/ x86_64/ subdirs, and the embedded
# buffer.bmir template), namespaced under <prefix>/include/classyc so it
# can't collide with anything else on the system. src/classyc.c's own header
# auto-discovery already probes "../include/classyc" relative to the binary.
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/classyc
)

# AOT support files classyc-aot (the installed script) needs at build time
# for every AOT program:
#   - mir-aot-runtime.c: GCC-compiled runtime glue (atomics, va_arg builtins,
#     etc). Ships as source and stays GCC-compiled per AOT build — classyc
#     cannot compile this file itself (it uses GCC/Clang atomic builtins,
#     thread_local registries, and __builtin_frame_address/return_address,
#     none of which the classyc frontend implements).
#   - mir-tls.c + the ext/mir headers: needed for mir-tls.c's own compile
#     step, and for `classyc-aot --with-mir` (e.g. bootstrapping classyc
#     itself against the real MIR API).
install(FILES ${CMAKE_SOURCE_DIR}/src/mir-aot-runtime.c
    DESTINATION ${CMAKE_INSTALL_DATADIR}/classyc/runtime
)
install(FILES ${CMAKE_SOURCE_DIR}/${MIRDIR}/mir-tls.c
    DESTINATION ${CMAKE_INSTALL_DATADIR}/classyc/mir
)
# Top-level ext/mir/*.h only (mir.h, mir-gen.h, and the handful of headers
# they transitively pull in) — NOT a recursive copy. ext/mir is the full
# vendored MIR checkout (its own c2mir/ front end, test suites, build
# artifacts); classyc's own build never references anything below its
# top level either (see the classyc target's include dirs above), so a
# non-recursive glob is both correct and avoids dragging that in.
file(GLOB CLASSYC_MIR_HEADERS "${CMAKE_SOURCE_DIR}/${MIRDIR}/*.h")
install(FILES ${CLASSYC_MIR_HEADERS}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/classyc/mir
)
install(FILES
    ${CMAKE_SOURCE_DIR}/ext/ccchan/cchan.h
    ${CMAKE_SOURCE_DIR}/ext/ccchan/minicoro.h
    DESTINATION ${CMAKE_INSTALL_DATADIR}/classyc/ccchan
)

# ---- uninstall --------------------------------------------------------------
# CMake has no built-in uninstall; this is the standard recipe (walk
# install_manifest.txt, which `install()` writes automatically on
# `cmake --install`, and remove every path it lists). Prefix-agnostic: works
# the same for the system and per-user builds since each has its own
# manifest in its own build directory.
if(NOT TARGET uninstall)
  configure_file(
      "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
      "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
      IMMEDIATE @ONLY
  )
  add_custom_target(uninstall
      COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/cmake_uninstall.cmake
      COMMENT "Removing installed classyc files (see install_manifest.txt)"
  )
endif()

message(STATUS "classyc build configured. MIR integrated as subdirectory.")
message(STATUS "Build with: cmake -B build -S . && cmake --build build")
message(STATUS "Output: bin/classyc and b2obj/b2objmac")
message(STATUS "Install with: cmake --install build   (uninstall: cmake --build build --target uninstall)")
