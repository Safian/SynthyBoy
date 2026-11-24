/*
 * SynthyBoy - CLAP extension "com.safian.bitmap-gui/1": a plug-in GUI the plug-in renders itself into a pixel buffer (docs/BITMAP-GUI-EXTENSION.md).
 * Copyright (C) 2026 Safian Szabolcs
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This header is the whole contract and it is meant to be copied into any host or plug-in (it needs nothing but <clap/clap.h>, the types of CLAP 1.2).
 * It is generic: nothing in it knows SynthyBoy.
 *
 *   plug-in side   SAFIAN_EXT_BITMAP_GUI       "com.safian.bitmap-gui/1"        returned by clap_plugin_t::get_extension ()
 *   host side      SAFIAN_EXT_BITMAP_GUI_HOST  "com.safian.bitmap-gui.host/1"   returned by clap_host_t::get_extension (); every function is optional
 *
 * What it is for. A host that cannot or must not host the plug-in's own toolkit (another version of the same library, no windowing system at all: the
 * framebuffer of a hardware sampler, a mobile app that already links a different build of the toolkit) lets the plug-in draw its GUI into memory. The host
 * only (1) calls create () with a size, (2) calls tick () regularly and render () when something changed, (3) copies the dirty rectangles of the returned
 * frame to its screen and (4) forwards touch / mouse / key input. Nothing of the plug-in's toolkit crosses this interface: pixels and plain C structs.
 *
 * Rules that hold for every function below:
 *  - MAIN THREAD ONLY, on both sides. The plug-in calls the host functions only from inside one of its own functions that the host called (or, when the host
 *    runs the platform event loop on the main thread, from a timer or an async message of that loop: still the main thread, between create () and destroy ()).
 *    The audio thread is never involved; parameters change through the normal CLAP parameter events.
 *  - Every function may be called before clap_plugin::activate () and after deactivate (): the GUI does not depend on the audio engine being active.
 *  - One GUI per plug-in instance. create () before everything else, destroy () before clap_plugin::destroy () (the plug-in destroys a forgotten GUI itself).
 *  - All coordinates are LOGICAL pixels (the unit of create (width, height)), with the origin at the top left of the GUI, x to the right, y down. Frame
 *    pixels and dirty rectangles are DEVICE pixels: logical * scale, rounded.
 *  - Strings are UTF-8 and owned by the caller; the callee copies what it keeps.
 *  - Struct layouts are frozen for "/1". An incompatible change is a new id string ("/2"); a plug-in may offer both.
 */
#pragma once

#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAFIAN_EXT_BITMAP_GUI "com.safian.bitmap-gui/1"
#define SAFIAN_EXT_BITMAP_GUI_HOST "com.safian.bitmap-gui.host/1"

/* ---- geometry and pixels ------------------------------------------------------------------------------------------------------------------------ */

/** A rectangle in device pixels of the frame (within [0, frame.width) x [0, frame.height)). */
typedef struct safian_bitmap_gui_rect
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} safian_bitmap_gui_rect_t;

/** Pixel formats of a frame. Only this one exists in "/1".
    BGRA8888_PREMUL: 32 bits per pixel, one pixel = the little-endian 32 bit word 0xAARRGGBB, i.e. the bytes in memory are B, G, R, A at any host endianness
    (the layout of a Windows DIB, of Core Graphics' kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, of Cairo ARGB32 and of a Linux fbdev
    "ARGB8888"). Colour channels are premultiplied by alpha. The GUI of a plug-in that paints an opaque background (SynthyBoy does) has alpha 255 in
    every pixel, where premultiplied and straight alpha are the same bytes. The first row is the top row. */
enum
{
    SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL = 1
};

/* ---- flags -------------------------------------------------------------------------------------------------------------------------------------- */

/** create () flags. */
enum
{
    /** The host runs the platform's native event loop on the calling thread (a macOS / Windows application with its own message loop, a GTK / Qt main
        loop): the plug-in's toolkit posts its timers and async messages into that loop and the host must NOT expect tick () to pump them. Without this flag
        (a framebuffer host, a test harness, a Linux host without a loop the toolkit can join) tick () and render () run the toolkit's own queue. */
    SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP = 1u << 0,
    /** The host has its own keys (a hardware keyboard or pads, an MPC / keyboard controller): the plug-in does not draw its on-screen piano strip and gives
        that area to the rest of the GUI. A plug-in without such a strip ignores the flag. */
    SAFIAN_BITMAP_GUI_CREATE_NO_ONSCREEN_KEYBOARD = 1u << 1
};

/** get_info () flags: what this GUI understands. */
enum
{
    SAFIAN_BITMAP_GUI_CAP_MULTITOUCH = 1u << 0,   /* several pointers at once (pointer ids): one finger is the primary button, two fingers are a pinch / pan gesture */
    SAFIAN_BITMAP_GUI_CAP_KEYBOARD = 1u << 1,     /* key_event () is used */
    SAFIAN_BITMAP_GUI_CAP_TEXT_INPUT = 1u << 2,   /* text fields exist: the host should offer a keyboard when request_text_input (show) arrives */
    SAFIAN_BITMAP_GUI_CAP_FILE_REQUESTS = 1u << 3 /* the GUI asks the host for files (and degrades when the host cannot) */
};

/** Modifier bits of pointer, wheel and key events. */
enum
{
    SAFIAN_BITMAP_GUI_MOD_SHIFT = 1u << 0,
    SAFIAN_BITMAP_GUI_MOD_CTRL = 1u << 1,
    SAFIAN_BITMAP_GUI_MOD_ALT = 1u << 2,
    SAFIAN_BITMAP_GUI_MOD_META = 1u << 3 /* Command on macOS, the Windows / Super key elsewhere */
};

/* ---- input -------------------------------------------------------------------------------------------------------------------------------------- */

enum
{
    SAFIAN_BITMAP_GUI_POINTER_DOWN = 1,   /* a button went down / a finger touched */
    SAFIAN_BITMAP_GUI_POINTER_MOVE = 2,   /* moved while down (a drag) */
    SAFIAN_BITMAP_GUI_POINTER_UP = 3,     /* released / lifted */
    SAFIAN_BITMAP_GUI_POINTER_CANCEL = 4, /* the host took the pointer away (palm rejection, a system gesture): the pointer is released like UP (the toolkit has no cancel) */
    SAFIAN_BITMAP_GUI_POINTER_HOVER = 5,  /* moved with no button down (mouse, pen in range); never sent for touch */
    SAFIAN_BITMAP_GUI_POINTER_LEAVE = 6   /* the pointer left the GUI area (hover state is cleared) */
};

enum
{
    SAFIAN_BITMAP_GUI_POINTER_KIND_MOUSE = 0,
    SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH = 1,
    SAFIAN_BITMAP_GUI_POINTER_KIND_PEN = 2
};

/** Button bits (buttons down AFTER the event). A touch is the primary button. */
enum
{
    SAFIAN_BITMAP_GUI_BUTTON_PRIMARY = 1u << 0,
    SAFIAN_BITMAP_GUI_BUTTON_SECONDARY = 1u << 1,
    SAFIAN_BITMAP_GUI_BUTTON_MIDDLE = 1u << 2
};

typedef struct safian_bitmap_gui_pointer_event
{
    uint32_t type;       /* SAFIAN_BITMAP_GUI_POINTER_* */
    uint32_t kind;       /* SAFIAN_BITMAP_GUI_POINTER_KIND_* */
    uint32_t pointer_id; /* identifies one finger / pen / the mouse from DOWN to UP / CANCEL; the host picks the ids (0 for the mouse), any 32 bit value, reusable after UP */
    double x;            /* logical pixels, fractions allowed */
    double y;
    uint32_t buttons;    /* SAFIAN_BITMAP_GUI_BUTTON_* bits down after this event */
    uint32_t modifiers;  /* SAFIAN_BITMAP_GUI_MOD_* */
    float pressure;      /* 0..1; 0 when unknown (treated as 1 while a button is down) */
} safian_bitmap_gui_pointer_event_t;

enum
{
    /** The deltas are precise (a trackpad / touchpad, in notches too but fractional and smooth) as opposed to the steps of a wheel. */
    SAFIAN_BITMAP_GUI_WHEEL_PRECISE = 1u << 0
};

typedef struct safian_bitmap_gui_wheel_event
{
    double x; /* where the pointer is, logical pixels */
    double y;
    double delta_x; /* in wheel notches (1.0 = one detent); positive = wheel rolled away from the user / content moves down (reveals what is above) / scroll right is positive x */
    double delta_y;
    uint32_t modifiers;
    uint32_t flags; /* SAFIAN_BITMAP_GUI_WHEEL_* */
} safian_bitmap_gui_wheel_event_t;

/** Key codes: a printable key is the Unicode code point of the character it makes WITHOUT shift, lower case ('a' = 0x61, '1' = 0x31, ' ' = 0x20, '/' ...);
    the keys that make no character have the codes below (stable, independent of any platform's key codes). Keypad keys use the codes of the characters
    they make. */
enum
{
    SAFIAN_BITMAP_GUI_KEY_BACKSPACE = 0x110001,
    SAFIAN_BITMAP_GUI_KEY_TAB = 0x110002,
    SAFIAN_BITMAP_GUI_KEY_ENTER = 0x110003,
    SAFIAN_BITMAP_GUI_KEY_ESCAPE = 0x110004,
    SAFIAN_BITMAP_GUI_KEY_DELETE = 0x110005,
    SAFIAN_BITMAP_GUI_KEY_INSERT = 0x110006,
    SAFIAN_BITMAP_GUI_KEY_HOME = 0x110007,
    SAFIAN_BITMAP_GUI_KEY_END = 0x110008,
    SAFIAN_BITMAP_GUI_KEY_PAGE_UP = 0x110009,
    SAFIAN_BITMAP_GUI_KEY_PAGE_DOWN = 0x11000a,
    SAFIAN_BITMAP_GUI_KEY_LEFT = 0x11000b,
    SAFIAN_BITMAP_GUI_KEY_RIGHT = 0x11000c,
    SAFIAN_BITMAP_GUI_KEY_UP = 0x11000d,
    SAFIAN_BITMAP_GUI_KEY_DOWN = 0x11000e,
    SAFIAN_BITMAP_GUI_KEY_F1 = 0x110020 /* F1 .. F12 = this + 0 .. 11 */
};

enum
{
    SAFIAN_BITMAP_GUI_KEY_DOWN_EVENT = 1,
    SAFIAN_BITMAP_GUI_KEY_UP_EVENT = 2
};

enum
{
    SAFIAN_BITMAP_GUI_KEY_FLAG_REPEAT = 1u << 0 /* an auto repeat of a key that is still down */
};

typedef struct safian_bitmap_gui_key_event
{
    uint32_t type;      /* SAFIAN_BITMAP_GUI_KEY_DOWN_EVENT / _UP_EVENT */
    uint32_t key_code;  /* see above */
    uint32_t modifiers; /* SAFIAN_BITMAP_GUI_MOD_* */
    uint32_t flags;     /* SAFIAN_BITMAP_GUI_KEY_FLAG_* */
    const char* text;   /* UTF-8 text this key press produces with the current modifiers and layout ("A", "5", "") or NULL; only on DOWN. A text field inserts it. */
} safian_bitmap_gui_key_event_t;

/* ---- size --------------------------------------------------------------------------------------------------------------------------------------- */

typedef struct safian_bitmap_gui_size_limits
{
    uint32_t min_width; /* logical pixels */
    uint32_t min_height;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t aspect_numerator; /* 0 / 0: any aspect ratio */
    uint32_t aspect_denominator;
} safian_bitmap_gui_size_limits_t;

typedef struct safian_bitmap_gui_info
{
    uint32_t flags;          /* SAFIAN_BITMAP_GUI_CAP_* */
    uint32_t pixel_format;   /* SAFIAN_BITMAP_GUI_PIXEL_* of every frame */
    uint32_t max_dirty_rects;/* a frame never lists more rectangles than this (the plug-in merges); at least 1 */
    double min_scale;        /* the device pixel ratios the plug-in renders; create () and set_scale () clamp into the range */
    double max_scale;
} safian_bitmap_gui_info_t;

/* ---- the frame ---------------------------------------------------------------------------------------------------------------------------------- */

enum
{
    /** The whole frame changed (first frame, after a resize or a scale change, an overlay that covers everything): dirty lists exactly one rectangle, the whole frame. */
    SAFIAN_BITMAP_GUI_FRAME_FULL = 1u << 0,
    /** The GUI is animating or shows live data (meters, a scope, a drag in progress): the host should tick () and render () again within next_frame_ms. */
    SAFIAN_BITMAP_GUI_FRAME_NEEDS_ANOTHER = 1u << 1
};

typedef struct safian_bitmap_gui_frame
{
    const uint8_t* pixels;  /* top left pixel; valid until the next render (), set_size (), set_scale () or destroy () of this GUI. Read only. */
    uint32_t width;         /* device pixels = round (logical width * scale) */
    uint32_t height;
    uint32_t stride_bytes;  /* distance between rows; at least width * 4, a multiple of 4 */
    uint32_t pixel_format;  /* SAFIAN_BITMAP_GUI_PIXEL_* */
    uint64_t frame_id;      /* starts at 1; increases by exactly one for every render () that changed pixels; the same value again means "unchanged" */
    uint32_t dirty_count;   /* 0: nothing changed since the previous render () (the pixels are still valid and identical) */
    const safian_bitmap_gui_rect_t* dirty; /* the rectangles that changed since the previous render () of this GUI, disjoint, inside the frame; valid like pixels */
    uint32_t flags;         /* SAFIAN_BITMAP_GUI_FRAME_* */
    uint32_t next_frame_ms; /* with FRAME_NEEDS_ANOTHER: the suggested time to the next tick (); otherwise 0. A hint, never a promise. */
} safian_bitmap_gui_frame_t;

/* ---- results of asynchronous host actions (the host answers the requests of the host extension below) ------------------------------------------------ */

enum
{
    SAFIAN_BITMAP_GUI_RESULT_OK = 0,        /* done; for a file request `path` is the chosen file (open: it exists) */
    SAFIAN_BITMAP_GUI_RESULT_CANCELLED = 1, /* the user cancelled */
    SAFIAN_BITMAP_GUI_RESULT_FAILED = 2     /* the host could not do it */
};

/* ---- plug-in side ------------------------------------------------------------------------------------------------------------------------------- */

typedef struct safian_plugin_bitmap_gui
{
    /* ---- life cycle ---- */

    /** Creates the GUI with an initial size in logical pixels and a device pixel ratio (1.0, 2.0, 1.5 ...; fractions allowed). `flags` are SAFIAN_BITMAP_GUI_CREATE_*.
        Looks up the host extension and keeps it. False: it could not (out of memory ...); nothing was created. Creating twice without destroy () is false. */
    bool (*create) (const clap_plugin_t* plugin, uint32_t width, uint32_t height, double scale, uint32_t flags);
    /** Destroys the GUI and releases the frame (the frame pointers die). Harmless without create (). Pending host requests are forgotten; late answers are ignored. */
    void (*destroy) (const clap_plugin_t* plugin);

    /* ---- size ---- */

    /** The capabilities of this GUI (valid before create ()). */
    bool (*get_info) (const clap_plugin_t* plugin, safian_bitmap_gui_info_t* info);
    /** The size the GUI would like to have, logical pixels (valid before create ()). */
    bool (*get_preferred_size) (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height);
    /** True when the host may choose any size within get_size_limits (). */
    bool (*can_resize) (const clap_plugin_t* plugin);
    bool (*get_size_limits) (const clap_plugin_t* plugin, safian_bitmap_gui_size_limits_t* limits);
    /** The current size, logical pixels (after create ()). */
    bool (*get_size) (const clap_plugin_t* plugin, uint32_t* width, uint32_t* height);
    /** Resizes to a size in logical pixels; the size is clamped into the limits and the result is what get_size () returns. The next frame is FRAME_FULL with the new
        pixel dimensions; the previous frame pointer is dead. False: no GUI, or the size was not accepted at all. */
    bool (*set_size) (const clap_plugin_t* plugin, uint32_t width, uint32_t height);
    /** A new device pixel ratio (the window moved to another display): the logical size stays, the frame gets new pixel dimensions and is FRAME_FULL. */
    bool (*set_scale) (const clap_plugin_t* plugin, double scale);

    /* ---- frames ---- */

    /** Runs what is due in the GUI: timers, animations, queued async messages of the toolkit (see CREATE_HOST_RUNS_EVENT_LOOP), data polling. Cheap when idle (microseconds,
        no drawing). Call it at the host's UI cadence while the GUI is visible, at least every 100 ms (every next_frame_ms of the last frame while it asks for more).
        Returns true when render () would produce a frame (same as is_dirty () after the work). */
    bool (*tick) (const clap_plugin_t* plugin);
    /** True when something changed since the last render (). Does not run anything, reads one flag: an idle GUI costs one call. */
    bool (*is_dirty) (const clap_plugin_t* plugin);
    /** Draws what changed into the frame buffer and describes it. Calls tick () first. When nothing changed the frame is returned unchanged with dirty_count 0 and the same
        frame_id. False only when there is no GUI. */
    bool (*render) (const clap_plugin_t* plugin, safian_bitmap_gui_frame_t* frame);

    /* ---- input; each returns true when the GUI used the event (the host may then not pass it on) ---- */

    bool (*pointer_event) (const clap_plugin_t* plugin, const safian_bitmap_gui_pointer_event_t* event);
    bool (*wheel_event) (const clap_plugin_t* plugin, const safian_bitmap_gui_wheel_event_t* event);
    bool (*key_event) (const clap_plugin_t* plugin, const safian_bitmap_gui_key_event_t* event);
    /** Committed text from a virtual keyboard / IME as UTF-8, inserted into the focused text field. */
    bool (*text_input) (const clap_plugin_t* plugin, const char* utf8);
    /** The GUI (the window, the screen area) gained / lost the keyboard focus. Losing it releases every pointer that is down (as CANCEL) and ends text editing. */
    void (*set_focus) (const clap_plugin_t* plugin, bool focused);

    /* ---- answers to host requests ---- */

    /** The answer to request_file () / share_file () with the id the plug-in gave: SAFIAN_BITMAP_GUI_RESULT_*; `path` is the chosen file for a file request that is OK, else NULL.
        Any time later, main thread; an unknown or already answered id is ignored. */
    void (*request_done) (const clap_plugin_t* plugin, uint32_t request_id, uint32_t result, const char* path);

    /* ---- remote controls (appended; the pages themselves are the plug-in's clap.remote-controls pages) ---- */

    /** The host's controller bank moved to the remote-controls page `page_id`: the GUI shows the panel that holds those parameters. False when it has no such panel. */
    bool (*show_remote_page) (const clap_plugin_t* plugin, clap_id page_id);
} safian_plugin_bitmap_gui_t;

/* ---- host side (all functions optional: NULL means "this host cannot", the plug-in degrades) -------------------------------------------------------- */

enum
{
    SAFIAN_BITMAP_GUI_CURSOR_DEFAULT = 0,
    SAFIAN_BITMAP_GUI_CURSOR_HIDDEN = 1,
    SAFIAN_BITMAP_GUI_CURSOR_POINTER = 2,     /* hand over something clickable */
    SAFIAN_BITMAP_GUI_CURSOR_TEXT = 3,
    SAFIAN_BITMAP_GUI_CURSOR_CROSSHAIR = 4,
    SAFIAN_BITMAP_GUI_CURSOR_RESIZE_H = 5,    /* left-right */
    SAFIAN_BITMAP_GUI_CURSOR_RESIZE_V = 6,    /* up-down */
    SAFIAN_BITMAP_GUI_CURSOR_RESIZE_ALL = 7,
    SAFIAN_BITMAP_GUI_CURSOR_GRAB = 8,
    SAFIAN_BITMAP_GUI_CURSOR_GRABBING = 9,
    SAFIAN_BITMAP_GUI_CURSOR_NOT_ALLOWED = 10
};

enum
{
    SAFIAN_BITMAP_GUI_TEXT_KIND_TEXT = 0,   /* free text */
    SAFIAN_BITMAP_GUI_TEXT_KIND_NUMBER = 1  /* a number with a unit ("1.5k", "-12 dB"): a keyboard with digits and a few letters is enough */
};

enum
{
    SAFIAN_BITMAP_GUI_FILE_OPEN = 0,
    SAFIAN_BITMAP_GUI_FILE_SAVE = 1
};

typedef struct safian_bitmap_gui_file_request
{
    uint32_t mode;             /* SAFIAN_BITMAP_GUI_FILE_OPEN / _SAVE */
    const char* title;         /* what the dialog says; may be empty */
    const char* wildcards;     /* "*.wav;*.flac", empty = any file */
    const char* suggested_name;/* save: the proposed file name, else empty */
    const char* purpose;       /* "sample", "wavetable", "impulse", "vital", "lfoshape", "export" ...: the host may remember the last folder per purpose */
} safian_bitmap_gui_file_request_t;

typedef struct safian_host_bitmap_gui
{
    /** Something changed and the GUI wants render () soon. `dirty` (logical pixels, may be NULL = unknown / everything) is only a hint for hosts that schedule redraws. Called
        when the first change after a render () happens, not for every repaint. */
    void (*request_render) (const clap_host_t* host, const safian_bitmap_gui_rect_t* dirty);
    /** The GUI wants a tick () at about this time (milliseconds from now); used when it has nothing dirty but a timer is due. */
    void (*request_tick) (const clap_host_t* host, uint32_t delay_ms);
    /** The GUI wants another size (logical pixels). True: the host will call set_size () (possibly with a different size, possibly later). */
    bool (*request_resize) (const clap_host_t* host, uint32_t width, uint32_t height);
    /** The cursor over the current pointer position (SAFIAN_BITMAP_GUI_CURSOR_*). Hosts without a cursor (touch) leave it NULL. */
    void (*set_cursor) (const clap_host_t* host, uint32_t cursor);
    /** A text field got the keyboard focus (show = true; `field` is its rectangle in logical pixels so the host can scroll it above the keyboard; `text` is its current content)
        or lost it (show = false, field and text NULL). The host shows or hides its virtual keyboard and sends text_input () / key_event () to the plug-in. */
    void (*request_text_input) (const clap_host_t* host, bool show, const safian_bitmap_gui_rect_t* field, uint32_t kind, const char* text);
    /** Asks the user for a file. True: the host accepted; it later calls request_done () of the plug-in with the same `request_id` exactly once (OK with the path, CANCELLED or
        FAILED). False: nothing will come. */
    bool (*request_file) (const clap_host_t* host, uint32_t request_id, const safian_bitmap_gui_file_request_t* request);
    /** Hands the file at `path` to the platform's share / export function. True: accepted; request_done () follows with OK or FAILED. */
    bool (*share_file) (const clap_host_t* host, uint32_t request_id, const char* path, const char* title);
    /** Opens a URL in the system's browser. */
    bool (*open_url) (const clap_host_t* host, const char* url);
    /** The panel the user works with changed: the host's controller bank (Q-Link) should show the clap.remote-controls page `page_id` of this plug-in. */
    void (*set_active_remote_page) (const clap_host_t* host, clap_id page_id);
} safian_host_bitmap_gui_t;

#ifdef __cplusplus
}
#endif
