#!/usr/bin/env python3
# SynthyBoy - the static UI library (synthyboy_static_ui, iOS) exports no name that another JUCE in the same binary could also define.
# Copyright (C) 2026 Safian Szabolcs
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Usage: scripts/check-static-ui-symbols.py [--namespace NAME] [--allow EXTRA_NAMESPACE]... <libsynthyboy_static_ui.a> [more archives...]
#        scripts/check-static-ui-symbols.py --self-test
#
# docs/ARCHITECTURE.md 4.3: the Kick app on iOS links JUCE 7 and this library (JUCE 8) into ONE binary. A static link sees hidden symbols too, so what keeps the two apart is the name: JUCE 8 is built
# in the C++ namespace NAME (default synthyboy_juce8, cmake/JuceNamespace.cmake) and its Objective-C classes carry the prefix NAME_. This script reads every symbol the archive DEFINES with external
# linkage (strong or weak: an inline function or a vtable of two JUCEs that merge under one name is an ODR violation, not harmless) and fails when
#   * a C++ name belongs to a namespace that is not ours (any "juce::", any other top level qualifier, a global C++ function or variable),
#   * an Objective-C class, metaclass or instance variable is not prefixed with NAME_,
#   * a plain C symbol is anything but synthyboy_clap_get_entry (the entry point of the static plug-in).
# Allowed namespaces: synthyboy (our code), NAME (JUCE 8), synthyboy_embedded_fonts (the fonts as data), std / __cxxabiv1 (the C++ library, weak instantiations), and --allow ones.
# The symbols the archive needs from outside (nm -u) are listed with --undefined; they are the frameworks and the C++ runtime of the host and have nothing to do with collisions.
#
# Exit status: 0 clean, 1 offending symbols, 2 usage. Environment: NM, CXXFILT, CC (self-test).

import os
import re
import subprocess
import sys
import tempfile

NM = os.environ.get("NM", "nm")
CXXFILT = os.environ.get("CXXFILT", "c++filt")
CC = os.environ.get("CC", "c++")

ENTRY = "synthyboy_clap_get_entry"
# compiler support functions, weak and identical in every translation unit of every library built with the same compiler (a copy in the host merges with ours)
COMPILER_SUPPORT = {"__clang_call_terminate"}
# words that stand before "::" in a demangled name without being a namespace ("... const::'lambda'()")
NOT_NAMESPACES = {"const", "volatile"}
# protocols of the Apple frameworks (NSObject, UIKeyInput, CALayerDelegate, UNUserNotificationCenterDelegate ...)
APPLE_PROTOCOL = re.compile(r"^(NS|UI|CA|UN|AV|CL|MT|WK|SC|CK|CF|MK)[A-Z]")


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", **kwargs).stdout


def defined_symbols(archive):
    """The external defined symbols of an archive: (member, symbol). nm -g -U -j: names only; members are announced by a line 'archive(member):'."""
    out = run([NM, "-g", "-U", "-j", archive])
    member = archive
    for line in out.splitlines():
        if not line:
            continue
        if line.endswith(":"):  # a symbol never ends with a colon
            member = line[:-1]
            continue
        yield member, line.strip()


def undefined_symbols(archive):
    out = run([NM, "-u", "-j", archive])
    return sorted({line.strip() for line in out.splitlines() if line.strip() and not line.endswith(":")})


def demangle_all(names):
    """Batch demangling: c++filt on stdin keeps the order and the line count."""
    if not names:
        return []
    proc = subprocess.run([CXXFILT], input="\n".join(names) + "\n", stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors="replace", check=True)
    result = proc.stdout.splitlines()
    if len(result) != len(names):
        raise RuntimeError("c++filt returned %d lines for %d names" % (len(result), len(names)))
    return result


QUALIFIER = re.compile(r"(?<![A-Za-z0-9_:~])([A-Za-z_][A-Za-z0-9_]*)::")


def check_archive(archive, namespace, allowed_namespaces, objc_prefix):
    """Returns the list of (member, symbol, reason)."""
    if not os.path.isfile(archive):
        print("check-static-ui-symbols: %s: no such file" % archive, file=sys.stderr)
        raise SystemExit(2)
    entries = list(defined_symbols(archive))
    names = [symbol for _, symbol in entries]
    # Mach-O prefixes every C symbol with one underscore; C++ names are __Z...
    stripped = [n[1:] if n.startswith("_") else n for n in names]
    demangled = demangle_all(stripped)
    allowed = set(allowed_namespaces) | {namespace}
    problems = []
    for (member, symbol), plain, text in zip(entries, stripped, demangled):
        if plain.startswith("_Z"):
            qualifiers = QUALIFIER.findall(text)
            foreign = sorted({q for q in qualifiers if q not in allowed and q not in NOT_NAMESPACES})
            if foreign:
                problems.append((member, text, "namespace " + ", ".join(foreign)))
            elif not [q for q in qualifiers if q not in NOT_NAMESPACES]:
                # a function or variable in the global namespace (operators and the like included): nothing keeps it apart from another library's
                problems.append((member, text, "global C++ name"))
        elif plain.startswith(("OBJC_CLASS_$_", "OBJC_METACLASS_$_", "OBJC_IVAR_$_")):
            cls = plain.split("$_", 1)[1]
            if not cls.startswith(objc_prefix):
                problems.append((member, symbol, "Objective-C name without the prefix " + objc_prefix))
        elif plain == ENTRY or plain in COMPILER_SUPPORT:
            continue
        elif plain.startswith(("_OBJC_PROTOCOL_$_", "_OBJC_LABEL_PROTOCOL_$_")):
            # the metadata of a protocol the code adopts; weak, and merged by the linker with the one of any other image. A protocol of the system (UIKeyInput, NSObject ...) is the same everywhere;
            # one that JUCE declares itself would need the prefix.
            name = plain.split("$_", 1)[1]
            if not (name.startswith(objc_prefix) or APPLE_PROTOCOL.match(name)):
                problems.append((member, symbol, "Objective-C protocol that is neither the system's nor prefixed"))
        else:
            problems.append((member, symbol, "C symbol"))
    return problems


def self_test():
    status = 0
    with tempfile.TemporaryDirectory() as d:
        def build(name, source):
            src = os.path.join(d, name + ".cpp")
            obj = os.path.join(d, name + ".o")
            lib = os.path.join(d, "lib" + name + ".a")
            with open(src, "w") as f:
                f.write(source)
            run([CC, "-std=c++17", "-c", src, "-o", obj])
            run(["ar", "rcs", lib, obj])
            return lib

        ok = build("ok", 'namespace synthyboy_juce8 { struct S { virtual ~S() {} int v() const; }; int S::v() const { return 1; } }\n'
                         'namespace synthyboy { int g() { return 2; } }\n'
                         'extern "C" const void* synthyboy_clap_get_entry() { return nullptr; }\n')
        juce7 = build("juce7", 'namespace juce { struct S { virtual ~S() {} int v() const; }; int S::v() const { return 1; } }\n')
        global_fn = build("globalfn", 'int sharedHelper() { return 3; }\n')
        c_sym = build("csym", 'extern "C" int png_create_read_struct() { return 4; }\n')
        other_ns = build("otherns", 'namespace BinaryData { extern const int x; const int x = 5; }\n')

        def expect(lib, clean, what):
            nonlocal status
            problems = check_archive(lib, "synthyboy_juce8", ["synthyboy", "std"], "synthyboy_juce8_")
            if bool(problems) == clean:
                print("self-test FAILED: %s" % what, file=sys.stderr)
                status = 1
            else:
                print("self-test: %s: ok" % what)

        expect(ok, True, "a library in synthyboy_juce8 / synthyboy with the entry point passes")
        expect(juce7, False, "a symbol in namespace juce (JUCE 7 as it is) is rejected")
        expect(global_fn, False, "a global C++ function is rejected")
        expect(c_sym, False, "a plain C symbol that is not the entry point is rejected")
        expect(other_ns, False, "a namespace that is not ours (BinaryData) is rejected")
    return status


def main(argv):
    namespace = "synthyboy_juce8"
    extra = []
    show_undefined = False
    archives = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--self-test":
            return self_test()
        if arg == "--namespace" and i + 1 < len(argv):
            namespace = argv[i + 1]
            i += 2
            continue
        if arg == "--allow" and i + 1 < len(argv):
            extra.append(argv[i + 1])
            i += 2
            continue
        if arg == "--undefined":
            show_undefined = True
        elif arg.startswith("-"):
            print(__doc__, file=sys.stderr)
            return 2
        else:
            archives.append(arg)
        i += 1
    if not archives:
        print("usage: check-static-ui-symbols.py [--namespace NAME] [--allow NS]... [--undefined] <archive.a>... | --self-test", file=sys.stderr)
        return 2
    allowed = ["synthyboy", "synthyboy_embedded_fonts", "std", "__cxxabiv1"] + extra
    status = 0
    for archive in archives:
        problems = check_archive(archive, namespace, allowed, namespace + "_")
        total = sum(1 for _ in defined_symbols(archive))
        if problems:
            status = 1
            print("check-static-ui-symbols: %s: %d of %d exported symbols are not ours:" % (archive, len(problems), total), file=sys.stderr)
            for member, symbol, reason in problems[:20]:
                print("    [%s] %s  (%s)" % (reason, symbol, member), file=sys.stderr)
        else:
            print("check-static-ui-symbols: %s: clean (%d exported symbols, all in %s, synthyboy, %s_* or the C++ library, plus %s)" % (archive, total, namespace, namespace, ENTRY))
        if show_undefined:
            print("undefined (needed from the host / the system):")
            for symbol in undefined_symbols(archive):
                print("    " + symbol)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
