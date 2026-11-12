# SynthyBoy - the partial link of the static UI library (cmake/StaticUi.cmake). Run with cmake -P by the build, never by hand.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Variables: COMPILER LIBTOOL SYSROOT ARCHS PLATFORM (ios | macos) MIN_VERSION ENVIRONMENT ("" | "-simulator") EXPORTS INPUTS OUTPUT WORK
#
# One ld -r per architecture over all members of all input archives (-all_load: the library is the unit, not the members); only the symbols named in EXPORTS stay global, the others become
# local symbols of the object file. The architectures are joined with lipo, the object goes into an archive of its own.

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(_export_options "")
foreach(_symbol ${EXPORTS})
    list(APPEND _export_options "-Wl,-exported_symbol,_${_symbol}")
endforeach()

set(_objects "")
foreach(_arch ${ARCHS})
    set(_object "${WORK}/${_arch}.o")
    execute_process(
        COMMAND "${COMPILER}" -r -target "${_arch}-apple-${PLATFORM}${MIN_VERSION}${ENVIRONMENT}" -isysroot "${SYSROOT}" -nostdlib
                -Wl,-all_load ${_export_options} -o "${_object}" ${INPUTS}
        RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "partial link for ${_arch} failed:\n${_error}")
    endif()
    list(APPEND _objects "${_object}")
endforeach()

list(LENGTH _objects _count)
if(_count EQUAL 1)
    set(_merged "${_objects}")
else()
    find_program(_lipo lipo REQUIRED)
    set(_merged "${WORK}/synthyboy_static_ui.o")
    execute_process(COMMAND "${_lipo}" -create ${_objects} -output "${_merged}" RESULT_VARIABLE _result ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "lipo failed:\n${_error}")
    endif()
endif()
# the member gets the name of the library: ar keeps names, and the object is what a linker prints in a message
get_filename_component(_name "${OUTPUT}" NAME_WE)
file(COPY_FILE "${_merged}" "${WORK}/${_name}.o")
file(REMOVE "${OUTPUT}")
execute_process(COMMAND "${LIBTOOL}" -static -o "${OUTPUT}" "${WORK}/${_name}.o" RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "libtool failed:\n${_error}")
endif()
