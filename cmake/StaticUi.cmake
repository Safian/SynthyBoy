# SynthyBoy - the finished static UI library (synthyboy_static_ui, iOS) and the list of what the app links with it.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# synthyboy_add_static_ui (<name> <parts target> <extra archive target>... EXPORT <symbol>...) adds the target <name> (ALL): it makes libsynthyboy_static_ui.a from the archives of the parts by one
# partial link (ld -r) of everything into ONE object file in which every symbol is local except the exported ones. A static link sees hidden symbols (visibility is a property of the
# final image), JUCE's embedded third party code (HarfBuzz, SheenBidi, the audio codecs, the VST3 SDK of juce_audio_processors) lives outside the JUCE namespace, and Objective-C classes
# are looked up by name at run time; the namespace and the class prefix (cmake/JuceNamespace.cmake) take care of the names the run time and the C++ code see, the partial link takes the
# rest off the symbol table, so the app's linker cannot find a second definition of anything. scripts/check-static-ui-symbols.py checks both stages.
#
# <name>.link.txt (in the build directory, generated): the archives of the packages below the UI library (synthyboy_core and what it needs: no JUCE, no Vital) and the frameworks that
# JUCE needs, i.e. what an Xcode project adds to its link besides libsynthyboy_static_ui.a.

function(_synthyboy_static_ui_walk dependency)
    string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" dependency "${dependency}")
    if(dependency MATCHES "\\$<" OR NOT TARGET "${dependency}")
        return()
    endif()
    get_target_property(_aliased "${dependency}" ALIASED_TARGET)
    if(_aliased)
        set(dependency "${_aliased}")
    endif()
    get_property(_seen GLOBAL PROPERTY _SYNTHYBOY_WALK_SEEN)
    if(dependency IN_LIST _seen)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY _SYNTHYBOY_WALK_SEEN "${dependency}")
    get_target_property(_type "${dependency}" TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
        set_property(GLOBAL APPEND PROPERTY _SYNTHYBOY_WALK_ARCHIVES "${dependency}")
    elseif(dependency MATCHES "^juce_")
        set_property(GLOBAL APPEND PROPERTY _SYNTHYBOY_WALK_MODULES "${dependency}")
    endif()
    get_target_property(_interface "${dependency}" INTERFACE_LINK_LIBRARIES)
    foreach(_next IN LISTS _interface)
        _synthyboy_static_ui_walk("${_next}")
    endforeach()
endfunction()

function(synthyboy_add_static_ui name parts)
    cmake_parse_arguments(ARG "" "" "ARCHIVES;EXPORT" ${ARGN})
    if(NOT CMAKE_LIBTOOL)
        find_program(CMAKE_LIBTOOL libtool REQUIRED)
    endif()
    # the platform of the triple of the partial link: iOS device, iOS simulator, or (to run the tests of this repository on the build machine) macOS
    set(_sysroot "${CMAKE_OSX_SYSROOT}")
    string(TOLOWER "${CMAKE_OSX_SYSROOT}" _sysroot_lower)
    if(IOS)
        set(_platform "ios")
        if(NOT _sysroot)
            message(FATAL_ERROR "${name}: CMAKE_OSX_SYSROOT is empty (an iOS build sets -DCMAKE_OSX_SYSROOT=iphoneos | iphonesimulator)")
        endif()
        if(_sysroot_lower MATCHES "simulator")
            set(_environment "-simulator")
        else()
            set(_environment "")
        endif()
    else()
        set(_platform "macos")
        set(_environment "")
        if(NOT _sysroot)
            execute_process(COMMAND xcrun --sdk macosx --show-sdk-path OUTPUT_VARIABLE _sysroot OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        set(_archs "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_archs "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        message(FATAL_ERROR "${name}: CMAKE_OSX_DEPLOYMENT_TARGET is empty")
    endif()

    set(_output "${CMAKE_CURRENT_BINARY_DIR}/lib${name}.a")
    set(_inputs "$<TARGET_FILE:${parts}>")
    foreach(_archive ${ARG_ARCHIVES})
        list(APPEND _inputs "$<TARGET_FILE:${_archive}>")
    endforeach()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}"
                "-DCOMPILER=${CMAKE_CXX_COMPILER}" "-DLIBTOOL=${CMAKE_LIBTOOL}" "-DSYSROOT=${_sysroot}"
                "-DARCHS=${_archs}" "-DPLATFORM=${_platform}" "-DMIN_VERSION=${CMAKE_OSX_DEPLOYMENT_TARGET}" "-DENVIRONMENT=${_environment}"
                "-DEXPORTS=${ARG_EXPORT}" "-DINPUTS=${_inputs}" "-DOUTPUT=${_output}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${name}-work"
                -P "${PROJECT_SOURCE_DIR}/cmake/FinishStaticUi.cmake"
        DEPENDS ${parts} ${ARG_ARCHIVES} "${PROJECT_SOURCE_DIR}/cmake/FinishStaticUi.cmake"
        COMMENT "${name}: partial link of the UI library, every symbol local except ${ARG_EXPORT}"
        VERBATIM)
    add_custom_target(${name} ALL DEPENDS "${_output}")

    # what the app links besides the library
    set_property(GLOBAL PROPERTY _SYNTHYBOY_WALK_SEEN "")
    set_property(GLOBAL PROPERTY _SYNTHYBOY_WALK_ARCHIVES "")
    set_property(GLOBAL PROPERTY _SYNTHYBOY_WALK_MODULES "")
    get_target_property(_roots ${parts} LINK_LIBRARIES)
    get_target_property(_roots_interface ${parts} INTERFACE_LINK_LIBRARIES)
    foreach(_root IN LISTS _roots _roots_interface)
        _synthyboy_static_ui_walk("${_root}")
    endforeach()
    get_property(_archives GLOBAL PROPERTY _SYNTHYBOY_WALK_ARCHIVES)
    get_property(_modules GLOBAL PROPERTY _SYNTHYBOY_WALK_MODULES)
    list(REMOVE_ITEM _archives ${parts} ${ARG_ARCHIVES})
    set(_link "# ${name}: what to link with lib${name}.a (generated; docs/BITMAP-GUI-EXTENSION.md section 14)\n# archives, in this order:\n")
    foreach(_archive ${_archives})
        string(APPEND _link "$<TARGET_FILE:${_archive}>\n")
    endforeach()
    string(APPEND _link "# frameworks (-framework NAME, weak ones -weak_framework NAME):\n")
    foreach(_module ${_modules})
        string(APPEND _link "$<JOIN:$<FILTER:$<TARGET_PROPERTY:${_module},INTERFACE_LINK_LIBRARIES>,INCLUDE,framework>,\n>\n")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}.link.txt" CONTENT "${_link}")

    # for the tests of this repository (tests/bitmap-gui): an executable that links the finished library the way the app does. The executables add_dependencies on ${name}.
    add_library(${name}_link INTERFACE)
    target_link_libraries(${name}_link INTERFACE "${_output}" synthyboy_core)
    foreach(_module ${_modules})
        target_link_libraries(${name}_link INTERFACE "$<FILTER:$<TARGET_PROPERTY:${_module},INTERFACE_LINK_LIBRARIES>,INCLUDE,framework>")
    endforeach()
endfunction()
