# SynthyBoy - JUCE in a C++ namespace and an Objective-C name space of its own (docs/ARCHITECTURE.md 4.3).
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Why: the Kick app on iOS is a JUCE 7 application and links SynthyBoy statically. Two JUCE versions in one binary would define the same C++ symbols (juce::MessageManager, ...) and
# the same Objective-C classes (JuceUIView, ...); hidden symbol visibility does not help, because a static link sees hidden symbols too. JUCE 8 has no namespace switch (JUCE_NAMESPACE is a
# deprecated macro that does nothing), so the name is changed at build time, in the compiler, for every translation unit of the build:
#
#   * C++:  -Djuce=<namespace>. The token "juce" is the namespace of all of JUCE (modules, the third party code inside them: zlib, libpng, libjpeg, Harfbuzz ... all live in juce::xxxNamespace),
#           so every juce::X becomes <namespace>::X, in the headers of our own code too (they say juce::). Header names (juce_core/juce_core.h), the JUCE_ macros and strings are other tokens.
#   * Objective-C: the classes that JUCE declares with a plain @interface (JuceUIView, JuceUIViewController, JuceUIWindow, JuceAppStartupDelegate, FileChooserControllerClass, ...) are registered
#           with the Objective-C runtime under their name; two images with one name make the runtime pick one of them silently. The list is read from the fetched JUCE sources (so it follows a JUCE update)
#           and every name gets the prefix <namespace>_ with a macro. JUCE's other classes are created at run time with a random suffix (ObjCClass), they cannot collide.
#
# synthyboy_juce_namespace (<namespace> <juce source dir>) adds the definitions to the directory it is called from (and the directories added after it).

function(synthyboy_juce_namespace ns juce_dir)
    if(NOT ns MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "SYNTHYBOY_JUCE_NAMESPACE must be a C identifier (got '${ns}')")
    endif()
    file(GLOB_RECURSE _files "${juce_dir}/modules/*.mm" "${juce_dir}/modules/*.h" "${juce_dir}/modules/*.cpp")
    set(_classes "")
    foreach(_file ${_files})
        # the class definitions only: "@interface Name :" (not categories "@interface Name (Category)", not forward declarations)
        file(STRINGS "${_file}" _lines REGEX "^[ \t]*@interface[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*:")
        foreach(_line ${_lines})
            string(REGEX REPLACE "^[ \t]*@interface[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*:.*$" "\\1" _class "${_line}")
            list(APPEND _classes "${_class}")
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _classes)
    list(SORT _classes)
    set(_definitions "juce=${ns}")
    foreach(_class ${_classes})
        list(APPEND _definitions "${_class}=${ns}_${_class}")
    endforeach()
    add_compile_definitions(${_definitions})
    list(LENGTH _classes _count)
    message(STATUS "JUCE namespace: juce -> ${ns}, ${_count} Objective-C classes prefixed (${_classes})")
    set(SYNTHYBOY_JUCE_OBJC_CLASSES "${_classes}" PARENT_SCOPE)
endfunction()
