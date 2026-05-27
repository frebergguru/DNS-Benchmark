cmake_minimum_required(VERSION 3.16)
project(dnsbenchmark LANGUAGES C VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(WITH_LDNS "Enable libldns for DNSSEC validation" OFF)
option(WITH_DOH   "Enable DNS-over-HTTPS transport (requires libcurl)" ON)
option(WITH_DOT   "Enable DNS-over-TLS transport (requires OpenSSL)"   ON)
option(BUILD_TESTING "Build unit tests" ON)
option(DNSB_WERROR   "Treat compiler warnings as errors" OFF)
option(DNSB_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    if(DNSB_WERROR)
        add_compile_options(-Werror)
    endif()
    if(DNSB_SANITIZE)
        # Catches the exact class of bugs the codebase has been burned by
        # (concurrent realloc vs qsort on stats samples). Enable with
        # -DDNSB_SANITIZE=ON for local dev and CI canary runs.
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gthread-2.0)

if(WITH_DOH)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
endif()
if(WITH_DOT)
    pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)
endif()
if(WITH_LDNS)
    pkg_check_modules(LDNS REQUIRED IMPORTED_TARGET libldns)
endif()

set(DNSB_CORE_SOURCES
    src/util/log.c
    src/util/strings.c
    src/util/i18n.c
    src/net/time_ns.c
    src/net/socket_compat.c
    src/dns/packet.c
    src/dns/transport_udp.c
    src/dns/transport_tcp.c
    src/engine/stats.c
    src/engine/engine.c
    src/io/resolvers_io.c
    src/io/csv_export.c
)

if(WITH_DOH)
    list(APPEND DNSB_CORE_SOURCES src/dns/transport_doh.c)
endif()
if(WITH_DOT)
    list(APPEND DNSB_CORE_SOURCES src/dns/transport_dot.c)
endif()

if(WIN32)
    list(APPEND DNSB_CORE_SOURCES src/platform/sysresolvers_win.c)
else()
    list(APPEND DNSB_CORE_SOURCES src/platform/sysresolvers_linux.c)
endif()

add_library(dnsb_core STATIC ${DNSB_CORE_SOURCES})
target_include_directories(dnsb_core PUBLIC src)
target_compile_definitions(dnsb_core PUBLIC
    DNSB_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(dnsb_core PUBLIC PkgConfig::GLIB)

if(WIN32)
    target_link_libraries(dnsb_core PUBLIC ws2_32 iphlpapi)
    target_compile_definitions(dnsb_core PUBLIC _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(dnsb_core PUBLIC Threads::Threads m)
endif()

if(WITH_DOH)
    target_link_libraries(dnsb_core PUBLIC PkgConfig::CURL)
    target_compile_definitions(dnsb_core PUBLIC DNSB_HAVE_DOH=1)
endif()
if(WITH_DOT)
    target_link_libraries(dnsb_core PUBLIC PkgConfig::OPENSSL)
    target_compile_definitions(dnsb_core PUBLIC DNSB_HAVE_DOT=1)
endif()
if(WITH_LDNS)
    target_link_libraries(dnsb_core PUBLIC PkgConfig::LDNS)
    target_compile_definitions(dnsb_core PUBLIC HAVE_LDNS=1)
endif()

add_executable(dnsbenchmark
    src/main.c
    src/ui/theme.c
    src/ui/window.c
    src/ui/tab_intro.c
    src/ui/tab_nameservers.c
    src/ui/tab_tabular.c
    src/ui/tab_conclusions.c
    src/ui/tab_help.c
    src/ui/chart.c
)
target_link_libraries(dnsbenchmark PRIVATE dnsb_core PkgConfig::GTK3)

target_compile_definitions(dnsbenchmark PRIVATE
    DNSB_DATADIR_BUILD="${CMAKE_CURRENT_SOURCE_DIR}/data"
    DNSB_DATADIR_INSTALL="${CMAKE_INSTALL_PREFIX}/share/dnsbenchmark"
)

install(TARGETS dnsbenchmark RUNTIME DESTINATION bin)
# Bundle the resolver list + test domains as the app's data tree.
install(FILES data/resolvers_default.tsv data/test_domains.txt
        DESTINATION share/dnsbenchmark)
# Standard freedesktop locations so the .desktop is discoverable and the
# icon shows up in app launchers + the GitHub-Releases AppImage.
install(FILES data/dnsbenchmark.desktop
        DESTINATION share/applications)
install(FILES data/icons/dnsbenchmark.svg
        DESTINATION share/icons/hicolor/scalable/apps)

# Convenience target: `cmake --build build --target appimage` produces
# DNS-Benchmark-<arch>.AppImage in the project root. See scripts/build-appimage.sh.
add_custom_target(appimage
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build-appimage.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building self-contained AppImage (downloads linuxdeploy on first run)"
    VERBATIM)

# ----- Translations (gettext) -----
find_package(Intl)
if(Intl_FOUND)
    target_include_directories(dnsb_core PUBLIC ${Intl_INCLUDE_DIRS})
    target_link_libraries(dnsb_core PUBLIC ${Intl_LIBRARIES})
endif()

find_program(MSGFMT_EXECUTABLE msgfmt)
set(DNSB_LOCALEDIR_BUILD   "${CMAKE_BINARY_DIR}/locale")
set(DNSB_LOCALEDIR_INSTALL "${CMAKE_INSTALL_PREFIX}/share/locale")
target_compile_definitions(dnsbenchmark PRIVATE
    DNSB_LOCALEDIR_BUILD="${DNSB_LOCALEDIR_BUILD}"
    DNSB_LOCALEDIR_INSTALL="${DNSB_LOCALEDIR_INSTALL}"
)
target_compile_definitions(dnsb_core PUBLIC
    DNSB_LOCALEDIR_BUILD="${DNSB_LOCALEDIR_BUILD}"
    DNSB_LOCALEDIR_INSTALL="${DNSB_LOCALEDIR_INSTALL}"
)

if(MSGFMT_EXECUTABLE)
    file(GLOB DNSB_PO_FILES "${CMAKE_SOURCE_DIR}/po/*.po")
    set(DNSB_MO_OUTPUTS "")
    foreach(po ${DNSB_PO_FILES})
        get_filename_component(lang ${po} NAME_WE)
        set(mo_out "${DNSB_LOCALEDIR_BUILD}/${lang}/LC_MESSAGES/dnsbenchmark.mo")
        add_custom_command(
            OUTPUT  ${mo_out}
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${DNSB_LOCALEDIR_BUILD}/${lang}/LC_MESSAGES"
            COMMAND ${MSGFMT_EXECUTABLE} -o ${mo_out} ${po}
            DEPENDS ${po}
            COMMENT "msgfmt: ${lang}.po"
        )
        list(APPEND DNSB_MO_OUTPUTS ${mo_out})
        # OPTIONAL so `cmake --install` doesn't error out on a fresh build
        # tree that hasn't run the `translations` target yet; the .mo file
        # is regenerated as soon as anything depends on it.
        install(FILES ${mo_out}
                DESTINATION share/locale/${lang}/LC_MESSAGES
                OPTIONAL)
    endforeach()
    if(DNSB_MO_OUTPUTS)
        add_custom_target(translations ALL DEPENDS ${DNSB_MO_OUTPUTS})
        add_dependencies(dnsbenchmark translations)
    endif()
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
