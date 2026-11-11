# SynthyBoy - the bitmap GUI of the native CLAP module (com.safian.bitmap-gui/1, docs/BITMAP-GUI-EXTENSION.md).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# fonts                    Roboto Regular + Bold as data (Apache-2.0, fonts/README.md): the glyphs do not depend on what the machine has installed
# synthyboy_bitmap_gui     (INTERFACE library, its sources are compiled in the module) the extension, the session, the peer that is a memory buffer, the UiHost of the native shell. Links synthyboy_ui (and with it the JUCE modules juce_gui_basics,
#                          juce_graphics, juce_events, juce_core, juce_data_structures, juce_audio_basics and, because the library's keyboard strip and sample import use them, juce_audio_formats and
#                          juce_audio_utils); the JUCE module sources are compiled into the module itself (synthyboy_clap), not into this static library.
# synthyboy_bitmap_module_options(target)   what the .clap needs besides the sources: hidden symbols, the export list (ONLY clap_entry and synthyboy_clap_get_entry leave the file),
#                          no fontconfig, dead code removal.

# The fonts as data, generated at configure time by a CMake script (no helper tool: the cross builds have no host tools, JUCE's juceaide would have to be built for the host first).
# Regenerated when a font or the script changes.
set(_font_files "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto-Regular.ttf" "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto-Bold.ttf")
set(_font_dir "${CMAKE_CURRENT_BINARY_DIR}/fonts")
set(_font_stamp "")
foreach(_f ${_font_files} "${CMAKE_CURRENT_SOURCE_DIR}/embed_fonts.cmake")
    file(SHA256 "${_f}" _h)
    string(APPEND _font_stamp "${_h}")
endforeach()
set(_font_stamp_file "${_font_dir}/stamp")
set(_font_old "")
if(EXISTS "${_font_stamp_file}")
    file(READ "${_font_stamp_file}" _font_old)
endif()
if(NOT _font_old STREQUAL _font_stamp OR NOT EXISTS "${_font_dir}/BinaryData.cpp")
    message(STATUS "bitmap GUI: embedding the fonts")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DOUT_DIR=${_font_dir}" "-DFILES=${_font_files}" -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_fonts.cmake" RESULT_VARIABLE _font_result)
    if(NOT _font_result EQUAL 0)
        message(FATAL_ERROR "embed_fonts.cmake failed")
    endif()
    file(WRITE "${_font_stamp_file}" "${_font_stamp}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_font_files} "${CMAKE_CURRENT_SOURCE_DIR}/embed_fonts.cmake")

# An INTERFACE library: its sources are compiled in the consumer (the module), next to the JUCE module sources, with the consumer's definitions (hidden symbols, no fontconfig, the export list).
set(_bitmap_sources BitmapGuiPlugin.cpp BitmapPeer.cpp BitmapSession.cpp JuceRuntime.cpp NativeUiHost.cpp PresetBrowser.cpp
    BitmapGuiPlugin.h BitmapPeer.h BitmapSession.h JuceRuntime.h NativeUiHost.h PresetBrowser.h)
list(TRANSFORM _bitmap_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
add_library(synthyboy_bitmap_gui INTERFACE)
target_sources(synthyboy_bitmap_gui INTERFACE ${_bitmap_sources})
target_sources(synthyboy_bitmap_gui INTERFACE "${_font_dir}/BinaryData.cpp")
target_include_directories(synthyboy_bitmap_gui INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.." "${_font_dir}" "${CLAP_INCLUDE_DIR}")
target_link_libraries(synthyboy_bitmap_gui INTERFACE synthyboy_ui)
target_compile_features(synthyboy_bitmap_gui INTERFACE cxx_std_17)
target_compile_definitions(synthyboy_bitmap_gui INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
if(TARGET juce::juce_recommended_config_flags)
    target_link_libraries(synthyboy_bitmap_gui INTERFACE juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# True when every member of the archive can be linked into a shared object: the archive is passed whole (--whole-archive) to a link of a tiny shared library. By default with -z text, so
# that a text relocation (the archive is not position independent) is an error and not a warning that produces a module with TEXTREL (a desktop module must not have one). A cross build for
# a device (the ARMv7 module of the Kick app: Debian's armhf archives carry text relocations, the module has always been linked with them and the device accepts them) allows them, so that it
# keeps FreeType inside and needs no library besides libc, libm, libstdc++ and libgcc_s. Only the linker is asked, the answer is cached per archive.
if(CMAKE_CROSSCOMPILING)
    set(_textrel_default ON)
else()
    set(_textrel_default OFF)
endif()
option(SYNTHYBOY_ALLOW_TEXTREL "Static archives with text relocations may go into the module (default: only when cross compiling)" ${_textrel_default})
function(synthyboy_archive_is_pic archive result)
    if(SYNTHYBOY_ALLOW_TEXTREL)
        set(_text_option "")
    else()
        set(_text_option -Wl,-z,text)
    endif()
    string(MD5 _key "${archive}${_text_option}")
    set(_var SYNTHYBOY_ARCHIVE_PIC_${_key})
    if(NOT DEFINED ${_var})
        set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/archive-pic-probe")
        file(WRITE "${_probe_dir}/probe.c" "int synthyboy_pic_probe(void) { return 0; }\n")
        try_compile(_probe_ok "${_probe_dir}/${_key}" SOURCES "${_probe_dir}/probe.c"
            COMPILE_DEFINITIONS -fPIC
            LINK_OPTIONS -shared ${_text_option} -Wl,--whole-archive "${archive}" -Wl,--no-whole-archive
            OUTPUT_VARIABLE _probe_log)
        set(${_var} ${_probe_ok} CACHE INTERNAL "the archive ${archive} can be linked into a shared object")
        if(NOT _probe_ok)
            file(WRITE "${_probe_dir}/${_key}.log" "${_probe_log}")
        endif()
    endif()
    set(${result} ${${_var}} PARENT_SCOPE)
endfunction()

function(synthyboy_bitmap_module_options target)
    # One JUCE per module (this one, 8.0.17), invisible to the host and to every other JUCE in the process (the Kick app links JUCE 7): hidden symbols everywhere, an export list
    # for the linker, no section of an archive exported by accident.
    set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    # The glyphs are the embedded Roboto: no system font lookup (fontconfig) in the module, nothing to find on a machine without fonts. No ALSA / JACK either: the GUI never opens an audio
    # device (juce_audio_utils pulls juce_audio_devices in, whose code is compiled out), so the .clap does not need libasound.
    target_compile_definitions(${target} PRIVATE JUCE_USE_FONTCONFIG=0 JUCE_ALSA=0 JUCE_JACK=0)
    if(APPLE)
        target_link_options(${target} PRIVATE
            "-Wl,-exported_symbols_list,${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clap_exports_macos.txt" -Wl,-dead_strip)
    elseif(UNIX)
        # FreeType (the one library JUCE needs for the glyph outlines on Linux) is linked statically when its archives are installed (libfreetype-dev has them): the .clap then needs no library
        # that the Kick app does not link already (the MPC's Buildroot). The archives come BEFORE the shared libraries of JUCE's package list on the link line, which --as-needed then drops.
        # An archive that cannot go into a shared object (not position independent: the x86_64 archives of Debian / Ubuntu) is probed out and its shared library is linked instead, see
        # synthyboy_archive_is_pic.
        option(SYNTHYBOY_STATIC_FREETYPE "Link FreeType statically into the module (Linux)" ON)
        if(SYNTHYBOY_STATIC_FREETYPE AND NOT APPLE)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(SYNTHYBOY_FT2 QUIET freetype2)
            endif()
            set(_ft_archives "")
            set(_ft_shared "")
            if(SYNTHYBOY_FT2_FOUND)
                set(_ft_ok TRUE)
                foreach(_lib IN LISTS SYNTHYBOY_FT2_STATIC_LIBRARIES)
                    if(_lib STREQUAL "m" OR _lib STREQUAL "pthread" OR _lib STREQUAL "dl")
                        continue()
                    endif()
                    # pkg-config leaves out the system directories: in a cross build only the multiarch directory of the TARGET may be searched (the host's archives are of another CPU)
                    if(CMAKE_CROSSCOMPILING AND CMAKE_LIBRARY_ARCHITECTURE)
                        find_library(SYNTHYBOY_FT2_ARCHIVE_${_lib} NAMES "lib${_lib}.a" PATHS "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
                    else()
                        find_library(SYNTHYBOY_FT2_ARCHIVE_${_lib} NAMES "lib${_lib}.a" HINTS ${SYNTHYBOY_FT2_STATIC_LIBRARY_DIRS})
                    endif()
                    if(SYNTHYBOY_FT2_ARCHIVE_${_lib})
                        # A distribution's static archives are position DEPENDENT or position independent EXECUTABLE code (Debian / Ubuntu: libpng16.a and libbz2.a of x86_64 reference
                        # stderr / stdout directly), which cannot go into a shared module (ld: "relocation R_X86_64_PC32 ... recompile with -fPIC"). Such an archive is not used: the
                        # module then links the shared library of that name instead (a dependency every desktop FreeType has anyway). Archives that pass the probe, which is how the
                        # ARMv7 module of the Kick app stays free of any library besides libc, libm, libstdc++ and libgcc_s, are linked statically as before.
                        synthyboy_archive_is_pic("${SYNTHYBOY_FT2_ARCHIVE_${_lib}}" _archive_pic)
                        if(_archive_pic)
                            list(APPEND _ft_archives "${SYNTHYBOY_FT2_ARCHIVE_${_lib}}")
                        else()
                            message(STATUS "bitmap GUI: ${SYNTHYBOY_FT2_ARCHIVE_${_lib}} is not position independent, linking lib${_lib} dynamically")
                            list(APPEND _ft_shared "${_lib}")
                        endif()
                    else()
                        set(_ft_ok FALSE)
                    endif()
                endforeach()
                if(_ft_ok AND _ft_archives)
                    # the archives first, the shared libraries of the ones that cannot be linked statically after them (a linker resolves from left to right)
                    target_link_libraries(${target} PRIVATE ${_ft_archives} ${_ft_shared})
                    message(STATUS "bitmap GUI: FreeType side: static archives (${_ft_archives}), shared libraries (${_ft_shared})")
                else()
                    message(STATUS "bitmap GUI: no static FreeType archives found, linking it dynamically")
                endif()
            endif()
        endif()
        target_link_options(${target} PRIVATE
            "-Wl,--version-script=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clap_exports.map" -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--as-needed)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endif()
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clap_exports.map" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clap_exports_macos.txt")
endfunction()
