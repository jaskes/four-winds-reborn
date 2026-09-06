cmake_minimum_required(VERSION 3.14)

project(FourWindsReborn VERSION 0.6.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SWE_WITH_JSON ON CACHE BOOL "enable json" FORCE)
set(SWE_WITH_LUA OFF CACHE BOOL "disable lua" FORCE)
set(SWE_EXAMPLES OFF CACHE BOOL "disable examples" FORCE)
if(ANDROID)
    # Rune War uses SWE's windowed GUI widgets for dialogs and the developer
    # console. Despite the historical option name, this is not a host terminal
    # dependency and must remain enabled on Android.
    set(SWE_DISABLE_TERMGUI OFF CACHE BOOL "enable SWE GUI widgets on Android" FORCE)
    set(SWE_DLOPEN OFF CACHE BOOL "disable dlopen on Android" FORCE)
    set(SWE_LOG_COLORED OFF CACHE BOOL "disable ANSI colors in logcat" FORCE)
else()
    set(SWE_DISABLE_TERMGUI OFF CACHE BOOL "enable termwin_gui" FORCE)
endif()

if(ANDROID)
    set(FOUR_WINDS_ANDROID_DEPS_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/android/.deps"
        CACHE PATH "Directory containing bootstrapped SDL2 Android sources")

    set(FOUR_WINDS_SDL2_DIR "${FOUR_WINDS_ANDROID_DEPS_DIR}/SDL2-2.32.10")
    set(FOUR_WINDS_SDL2_IMAGE_DIR "${FOUR_WINDS_ANDROID_DEPS_DIR}/SDL2_image-2.8.12")
    set(FOUR_WINDS_SDL2_MIXER_DIR "${FOUR_WINDS_ANDROID_DEPS_DIR}/SDL2_mixer-2.8.2")
    set(FOUR_WINDS_SDL2_TTF_DIR "${FOUR_WINDS_ANDROID_DEPS_DIR}/SDL2_ttf-2.24.0")
    foreach(dep_dir IN ITEMS
            FOUR_WINDS_SDL2_DIR
            FOUR_WINDS_SDL2_IMAGE_DIR
            FOUR_WINDS_SDL2_MIXER_DIR
            FOUR_WINDS_SDL2_TTF_DIR)
        if(NOT EXISTS "${${dep_dir}}/CMakeLists.txt")
            message(FATAL_ERROR
                "Missing Android dependency ${dep_dir}: ${${dep_dir}}. "
                "Run scripts/bootstrap-android.ps1 first.")
        endif()
    endforeach()

    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(SDL_SHARED ON CACHE BOOL "Build SDL2 shared for SDLActivity" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Do not duplicate SDL2 inside libmain" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Disable SDL2 test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Disable SDL2 tests" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "Disable SDL2 install targets" FORCE)
    add_subdirectory("${FOUR_WINDS_SDL2_DIR}" android-sdl2 EXCLUDE_FROM_ALL)

    set(BUILD_SHARED_LIBS ON CACHE BOOL "Build SDL satellite libraries shared" FORCE)
    set(SDL2IMAGE_INSTALL OFF CACHE BOOL "Disable SDL2_image install target" FORCE)
    set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "Disable SDL2_image samples" FORCE)
    set(SDL2IMAGE_TESTS OFF CACHE BOOL "Disable SDL2_image tests" FORCE)
    set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "Use dependency-free PNG/JPEG loader" FORCE)
    set(SDL2IMAGE_AVIF OFF CACHE BOOL "Disable unused AVIF support" FORCE)
    set(SDL2IMAGE_GIF OFF CACHE BOOL "Disable unused GIF support" FORCE)
    set(SDL2IMAGE_JXL OFF CACHE BOOL "Disable unused JXL support" FORCE)
    set(SDL2IMAGE_LBM OFF CACHE BOOL "Disable unused LBM support" FORCE)
    set(SDL2IMAGE_PCX OFF CACHE BOOL "Disable unused PCX support" FORCE)
    set(SDL2IMAGE_PNM OFF CACHE BOOL "Disable unused PNM support" FORCE)
    set(SDL2IMAGE_QOI OFF CACHE BOOL "Disable unused QOI support" FORCE)
    set(SDL2IMAGE_SVG OFF CACHE BOOL "Disable unused SVG support" FORCE)
    set(SDL2IMAGE_TGA OFF CACHE BOOL "Disable unused TGA support" FORCE)
    set(SDL2IMAGE_TIF OFF CACHE BOOL "Disable unused TIFF support" FORCE)
    set(SDL2IMAGE_WEBP OFF CACHE BOOL "Disable unused WebP support" FORCE)
    set(SDL2IMAGE_XCF OFF CACHE BOOL "Disable unused XCF support" FORCE)
    set(SDL2IMAGE_XPM OFF CACHE BOOL "Disable unused XPM support" FORCE)
    set(SDL2IMAGE_XV OFF CACHE BOOL "Disable unused XV support" FORCE)
    add_subdirectory("${FOUR_WINDS_SDL2_IMAGE_DIR}" android-sdl2-image EXCLUDE_FROM_ALL)

    set(SDL2MIXER_INSTALL OFF CACHE BOOL "Disable SDL2_mixer install target" FORCE)
    set(SDL2MIXER_DEPS_SHARED OFF CACHE BOOL "Link codec helpers into SDL2_mixer" FORCE)
    set(SDL2MIXER_VENDORED ON CACHE BOOL "Use bundled lightweight codecs" FORCE)
    set(SDL2MIXER_SAMPLES OFF CACHE BOOL "Disable SDL2_mixer samples" FORCE)
    set(SDL2MIXER_CMD OFF CACHE BOOL "Disable external music commands" FORCE)
    set(SDL2MIXER_FLAC OFF CACHE BOOL "Disable unused FLAC support" FORCE)
    set(SDL2MIXER_GME OFF CACHE BOOL "Disable unused GME support" FORCE)
    set(SDL2MIXER_MOD OFF CACHE BOOL "Disable unused module support" FORCE)
    set(SDL2MIXER_MP3 OFF CACHE BOOL "Disable unused MP3 support" FORCE)
    set(SDL2MIXER_MIDI ON CACHE BOOL "Keep classic MIDI soundtrack support" FORCE)
    set(SDL2MIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "Disable FluidSynth" FORCE)
    set(SDL2MIXER_MIDI_TIMIDITY ON CACHE BOOL "Use built-in Timidity MIDI player" FORCE)
    set(SDL2MIXER_OPUS OFF CACHE BOOL "Disable unused Opus support" FORCE)
    set(SDL2MIXER_VORBIS STB CACHE STRING "Use built-in OGG Vorbis decoder" FORCE)
    set(SDL2MIXER_WAVE ON CACHE BOOL "Keep WAV support" FORCE)
    set(SDL2MIXER_WAVPACK OFF CACHE BOOL "Disable unused WavPack support" FORCE)
    add_subdirectory("${FOUR_WINDS_SDL2_MIXER_DIR}" android-sdl2-mixer EXCLUDE_FROM_ALL)

    set(SDL2TTF_INSTALL OFF CACHE BOOL "Disable SDL2_ttf install target" FORCE)
    set(SDL2TTF_VENDORED ON CACHE BOOL "Use vendored FreeType" FORCE)
    set(SDL2TTF_SAMPLES OFF CACHE BOOL "Disable SDL2_ttf samples" FORCE)
    set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "Disable optional HarfBuzz" FORCE)
    add_subdirectory("${FOUR_WINDS_SDL2_TTF_DIR}" android-sdl2-ttf EXCLUDE_FROM_ALL)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DBUILD_DEBUG")

add_subdirectory(engine)
include(cmake/MultiplayerTLS.cmake)

set(FOUR_WINDS_GAME_REVISION "unknown")
set(FOUR_WINDS_ENGINE_REVISION "unknown")
set(FOUR_WINDS_RELEASE_TAG "")
string(TIMESTAMP FOUR_WINDS_BUILD_DATE "%Y-%m-%d")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_CURRENT_SOURCE_DIR}"
            -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE FOUR_WINDS_GAME_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_CURRENT_SOURCE_DIR}"
            -C "${CMAKE_CURRENT_SOURCE_DIR}" diff --quiet --ignore-space-at-eol
            --ignore-submodules=dirty
        RESULT_VARIABLE FOUR_WINDS_GAME_DIRTY
        ERROR_QUIET)
    if(FOUR_WINDS_GAME_DIRTY EQUAL 1)
        string(APPEND FOUR_WINDS_GAME_REVISION "-dirty")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_CURRENT_SOURCE_DIR}"
            -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --exact-match --match "v[0-9]*"
        OUTPUT_VARIABLE FOUR_WINDS_RELEASE_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_CURRENT_SOURCE_DIR}/engine"
            -C "${CMAKE_CURRENT_SOURCE_DIR}/engine" rev-parse HEAD
        OUTPUT_VARIABLE FOUR_WINDS_ENGINE_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c "safe.directory=${CMAKE_CURRENT_SOURCE_DIR}/engine"
            -C "${CMAKE_CURRENT_SOURCE_DIR}/engine" diff --quiet --ignore-space-at-eol
        RESULT_VARIABLE FOUR_WINDS_ENGINE_DIRTY
        ERROR_QUIET)
    if(FOUR_WINDS_ENGINE_DIRTY EQUAL 1)
        string(APPEND FOUR_WINDS_ENGINE_REVISION "-dirty")
    endif()
endif()
if(NOT FOUR_WINDS_GAME_REVISION)
    set(FOUR_WINDS_GAME_REVISION "unknown")
endif()
if(NOT FOUR_WINDS_ENGINE_REVISION)
    set(FOUR_WINDS_ENGINE_REVISION "unknown")
endif()
if(NOT FOUR_WINDS_RELEASE_TAG STREQUAL "v${PROJECT_VERSION}" OR FOUR_WINDS_GAME_DIRTY EQUAL 1)
    set(FOUR_WINDS_RELEASE_TAG "")
endif()

option(FOUR_WINDS_WINDOWS_GUI
    "Build the Windows executable without an attached console"
    OFF)

set(FOUR_WINDS_BUILD_DEFINITIONS
    FOUR_WINDS_VERSION="${PROJECT_VERSION}"
    FOUR_WINDS_BUILD_DATE="${FOUR_WINDS_BUILD_DATE}"
    FOUR_WINDS_RELEASE_TAG="${FOUR_WINDS_RELEASE_TAG}"
    FOUR_WINDS_GAME_REVISION="${FOUR_WINDS_GAME_REVISION}"
    FOUR_WINDS_ENGINE_REVISION="${FOUR_WINDS_ENGINE_REVISION}")

set(FOUR_WINDS_SOURCE
    src/gamedata.cpp
    src/runewars.cpp
    src/gameobjects.cpp

    src/platform/crashreport.cpp
    src/platform/contentcatalog.cpp
    src/platform/filedialog.cpp
    src/platform/recovery.cpp
    src/platform/replayfiles.cpp
    src/platform/savegames.cpp
    src/platform/settings.cpp

    src/network/clientview.cpp
    src/network/commandwire.cpp
    src/network/matchauthority.cpp
    src/network/matchsession.cpp
    src/network/networkaddress.cpp
    src/network/securetransport.cpp
    src/network/tcptransport.cpp
    src/network/wirejson.cpp

    src/rules/actionvalidation.cpp
    src/rules/adventurecommands.cpp
    src/rules/contentpackage.cpp
    src/rules/creaturestate.cpp
    src/rules/developerfixtures.cpp
    src/rules/gamestateio.cpp
    src/rules/gameplayrng.cpp
    src/rules/matchtopology.cpp
    src/rules/matchscore.cpp
    src/rules/playerstate.cpp
    src/rules/replay.cpp
    src/rules/runegamecommands.cpp
    src/rules/runegameruleset.cpp
    src/rules/runegamescoring.cpp
    src/rules/runegamestate.cpp

    src/battle/battle.cpp
    src/battle/battlecommands.cpp
    src/battle/battleentities.cpp
    src/battle/battlesession.cpp
    src/battle/battlestate.cpp

    src/ai/aiadventure.cpp
    src/ai/aibattle.cpp
    src/ai/aiprofile.cpp
    src/ai/aispell.cpp
    src/ai/aistrategy.cpp
    src/ai/aiturn.cpp
    src/ai/simulation.cpp

    src/ui/adventurehints.cpp
    src/ui/adventureinput.cpp
    src/ui/adventurepart.cpp
    src/ui/battlechoicedialog.cpp
    src/ui/battlesummarypart.cpp
    src/ui/battleuitext.cpp
    src/ui/developertools.cpp
    src/ui/dialogs.cpp
    src/ui/encyclopedia.cpp
    src/ui/gamesummarypart.cpp
    src/ui/gametheme.cpp
    src/ui/intropart.cpp
    src/ui/jsongui.cpp
    src/ui/loadrecovery.cpp
    src/ui/mahjongpart.cpp
    src/ui/mahjongsummarypart.cpp
    src/ui/mainmenu.cpp
    src/ui/multiplayerlobby.cpp
    src/ui/replaybrowser.cpp
    src/ui/replayviewer.cpp
    src/ui/runegameinput.cpp
    src/ui/runegamepresentation.cpp
    src/ui/runegametargetdialogs.cpp
    src/ui/runegamewidgets.cpp
    src/ui/selectperson.cpp
    src/ui/settingsmenu.cpp
    src/ui/showplayers.cpp
    src/ui/strings.cpp)

include_directories(
    engine
    src
    src/ai
    src/battle
    src/platform
    src/rules
    src/ui
    src/network)
if(ANDROID)
    add_library(four_winds_reborn SHARED ${FOUR_WINDS_SOURCE})
    set_target_properties(four_winds_reborn PROPERTIES OUTPUT_NAME main)
else()
    add_executable(four_winds_reborn ${FOUR_WINDS_SOURCE})
endif()
target_compile_definitions(four_winds_reborn PRIVATE
    ${FOUR_WINDS_BUILD_DEFINITIONS}
    $<$<BOOL:${ANDROID}>:ANDROID>)

target_link_libraries(four_winds_reborn libswe mbedtls mbedx509 mbedcrypto)
if(WIN32)
    target_link_libraries(four_winds_reborn dbghelp comdlg32 ws2_32 bcrypt iphlpapi)
endif()
if(NOT ANDROID)
    set_target_properties(four_winds_reborn PROPERTIES
        OUTPUT_NAME four-winds-reborn
        WIN32_EXECUTABLE ${FOUR_WINDS_WINDOWS_GUI})
endif()

option(BUILD_TESTING "Build gameplay regression tests" OFF)
include(CTest)
if(BUILD_TESTING AND NOT ANDROID)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME theme_data_validation
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-json.py")

    add_executable(gameplay_regression_tests
        tests/gameplay_regression_tests.cpp
        tests/avatar_balance_tests.cpp
        tests/local_modes_ui_tests.cpp
        tests/state_integrity_tests.cpp
        tests/clientview_tests.cpp
        tests/matchauthority_tests.cpp
        tests/multiplayer_process_peer.cpp
        tests/session_protocol_tests.cpp
        tests/multiplayer_ui_tests.cpp
        tests/network_address_tests.cpp
        src/ai/tournament.cpp
        ${FOUR_WINDS_SOURCE})
    target_compile_definitions(gameplay_regression_tests PRIVATE
        RUNEWARS_NO_MAIN
        ${FOUR_WINDS_BUILD_DEFINITIONS}
        FOUR_WINDS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(gameplay_regression_tests libswe mbedtls mbedx509 mbedcrypto)
    if(WIN32)
        target_link_libraries(gameplay_regression_tests dbghelp comdlg32 ws2_32 bcrypt iphlpapi)
    endif()
    add_test(NAME gameplay_regressions COMMAND gameplay_regression_tests)
    add_test(NAME local_modes_ui COMMAND gameplay_regression_tests --local-modes-ui-self-test)
    add_test(NAME state_integrity COMMAND gameplay_regression_tests --state-integrity-self-test)
    add_test(NAME multiplayer_client_view COMMAND gameplay_regression_tests --client-view-self-test)
    add_test(NAME multiplayer_authority COMMAND gameplay_regression_tests --match-authority-self-test)
    add_test(NAME multiplayer_session_protocol COMMAND gameplay_regression_tests --session-protocol-self-test)
    add_test(NAME multiplayer_ui COMMAND gameplay_regression_tests --multiplayer-ui-self-test)
    add_test(NAME multiplayer_addresses COMMAND gameplay_regression_tests --network-address-self-test)
    set_tests_properties(multiplayer_ui PROPERTIES TIMEOUT 90
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software;FOUR_WINDS_SETTINGS_FILE=${CMAKE_BINARY_DIR}/multiplayer-ui/settings.json;FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/multiplayer-ui/recovery;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/multiplayer-ui/saves;FOUR_WINDS_UI_SNAPSHOT_DIR=${CMAKE_BINARY_DIR}/multiplayer-ui/screenshots")
    add_test(NAME multiplayer_process_duel COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/multiplayer_process_test.py"
        --executable $<TARGET_FILE:gameplay_regression_tests>)
    set_tests_properties(multiplayer_process_duel PROPERTIES TIMEOUT 260)
    add_test(NAME multiplayer_process_combat COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/multiplayer_process_test.py"
        --executable $<TARGET_FILE:gameplay_regression_tests> --combat)
    set_tests_properties(multiplayer_process_combat PROPERTIES TIMEOUT 260)
    add_test(NAME multiplayer_process_reconnect COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/multiplayer_process_test.py"
        --executable $<TARGET_FILE:gameplay_regression_tests> --reconnect)
    set_tests_properties(multiplayer_process_reconnect PROPERTIES TIMEOUT 260)
    foreach(network_case IN ITEMS ffa coalition coalition_ai)
        if(network_case STREQUAL "ffa")
            set(network_mode classic-ffa)
            set(network_humans 4)
        elseif(network_case STREQUAL "coalition")
            set(network_mode coalition)
            set(network_humans 4)
        else()
            set(network_mode coalition)
            set(network_humans 2)
        endif()
        add_test(NAME multiplayer_process_${network_case} COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/multiplayer_process_test.py"
            --executable $<TARGET_FILE:gameplay_regression_tests>
            --mode ${network_mode} --humans ${network_humans})
        set_tests_properties(multiplayer_process_${network_case} PROPERTIES TIMEOUT 260)
    endforeach()
    add_executable(network_transport_tests tests/network_transport_tests.cpp
        src/network/tcptransport.cpp src/network/wirejson.cpp)
    target_link_libraries(network_transport_tests libswe)
    if(WIN32)
        target_link_libraries(network_transport_tests ws2_32 bcrypt)
    endif()
    add_test(NAME multiplayer_transport COMMAND network_transport_tests)
    add_executable(secure_transport_tests tests/secure_transport_tests.cpp
        src/network/tcptransport.cpp src/network/securetransport.cpp)
    target_link_libraries(secure_transport_tests mbedtls mbedx509 mbedcrypto)
    if(WIN32)
        target_link_libraries(secure_transport_tests ws2_32 bcrypt)
    endif()
    add_test(NAME multiplayer_secure_transport COMMAND secure_transport_tests)
    set_tests_properties(multiplayer_secure_transport PROPERTIES TIMEOUT 90)
    set_tests_properties(multiplayer_transport multiplayer_authority multiplayer_client_view PROPERTIES TIMEOUT 120)
    add_test(NAME match_mode_matrix COMMAND gameplay_regression_tests --match-mode-matrix-self-test)
    set_tests_properties(match_mode_matrix PROPERTIES
        TIMEOUT 240
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/match-mode-matrix/recovery;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/match-mode-matrix/saves")
    set_tests_properties(state_integrity PROPERTIES
        TIMEOUT 90
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/state-integrity/recovery;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/state-integrity/saves")
    set_tests_properties(local_modes_ui PROPERTIES
        TIMEOUT 90
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_RENDER_DRIVER=software;FOUR_WINDS_SETTINGS_FILE=${CMAKE_BINARY_DIR}/local-modes-ui/settings.json;FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/local-modes-ui/recovery;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/local-modes-ui/saves")
    set_tests_properties(gameplay_regressions PROPERTIES
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-gameplay;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-gameplay")
    add_test(NAME avatar_balance_metrics COMMAND gameplay_regression_tests --balance-only)
    set_tests_properties(avatar_balance_metrics PROPERTIES
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-balance;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-balance")
    add_test(NAME crash_reporting COMMAND gameplay_regression_tests --crash-report-self-test)
    set_tests_properties(crash_reporting PROPERTIES
        ENVIRONMENT "FOUR_WINDS_DIAGNOSTICS_DIR=${CMAKE_BINARY_DIR}/diagnostics-test;FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-crash;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-crash")
    add_test(NAME recovery_checkpointing COMMAND gameplay_regression_tests --recovery-self-test)
    set_tests_properties(recovery_checkpointing PROPERTIES
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test")
    add_test(NAME settings_persistence COMMAND gameplay_regression_tests --settings-self-test)
    set_tests_properties(settings_persistence PROPERTIES
        ENVIRONMENT "FOUR_WINDS_SETTINGS_FILE=${CMAKE_BINARY_DIR}/settings-test/settings.json;FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-settings;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-settings")
    add_test(NAME fixed_seed_replay COMMAND gameplay_regression_tests --fixed-seed-replay)
    set_tests_properties(fixed_seed_replay PROPERTIES
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-replay;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-replay")
    add_test(NAME headless_match_simulation
        COMMAND gameplay_regression_tests --headless-match-self-test)
    set_tests_properties(headless_match_simulation PROPERTIES
        ENVIRONMENT "FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-headless;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-headless")
    if(WIN32)
        add_test(NAME windows_native_crash_reporting
            COMMAND gameplay_regression_tests --windows-crash-report-self-test)
        set_tests_properties(windows_native_crash_reporting PROPERTIES
            ENVIRONMENT "FOUR_WINDS_DIAGNOSTICS_DIR=${CMAKE_BINARY_DIR}/diagnostics-native-test;FOUR_WINDS_RECOVERY_DIR=${CMAKE_BINARY_DIR}/recovery-test-native-crash;FOUR_WINDS_SAVE_DIR=${CMAKE_BINARY_DIR}/save-test-native-crash")
    endif()
endif()
