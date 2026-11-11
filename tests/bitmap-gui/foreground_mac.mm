// SynthyBoy - tests of the bitmap GUI: on macOS a pop-up menu or a call-out of the toolkit closes itself when the process is not the active application. A console test host is not,
// so it activates itself (no Dock icon: accessory policy). A real host is the active application while its user works with the GUI.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#import <Cocoa/Cocoa.h>

bool bgtestMakeProcessForeground()
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];
        [NSApp activateIgnoringOtherApps: YES];
        for (int i = 0; i < 100 && ! [NSApp isActive]; ++i)
            [[NSRunLoop currentRunLoop] runUntilDate: [NSDate dateWithTimeIntervalSinceNow: 0.02]];
        return [NSApp isActive];
    }
}
