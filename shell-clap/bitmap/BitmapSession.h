// SynthyBoy - bitmap GUI: one GUI of one plug-in instance (the object behind create () / destroy ()).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Owns the editor (shell-juce/ui, the same SynthyBoyEditor the JUCE plug-in shows), the peer-backed root component it lives in, the software image it is drawn into and the list of dirty
// rectangles. Main thread only; every public function is an entry point of the extension and starts with JuceRuntime::enter ().
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "BitmapPeer.h"
#include "NativeUiHost.h"
#include "bitmap_gui_ext.h"
#include "synthyboy_plugin.h"
#include "ui/Editor.h"

namespace synthyboy::bitmap
{
class BitmapSession final : public BitmapGuiHandle, private BitmapPeer::Client, private ui::ParameterStore::Listener
{
public:
    static constexpr uint32_t kMaxDirtyRects = 16;
    static constexpr double kMinScale = 0.5, kMaxScale = 4.0;
    static constexpr uint32_t kDefaultWidth = 1024, kDefaultHeight = 768;

    BitmapSession (SynthyBoyClap& plugin, uint32_t width, uint32_t height, double scale, uint32_t flags);
    ~BitmapSession() override;

    bool setSize (uint32_t width, uint32_t height);
    bool setScale (double newScale);
    void getSize (uint32_t& width, uint32_t& height) const noexcept { width = static_cast<uint32_t> (logicalWidth); height = static_cast<uint32_t> (logicalHeight); }

    bool tick();
    bool isDirty() const noexcept { return fullPending || ! dirtyLogical.isEmpty(); }
    bool render (safian_bitmap_gui_frame_t& frame);

    bool pointerEvent (const safian_bitmap_gui_pointer_event_t& event);
    bool wheelEvent (const safian_bitmap_gui_wheel_event_t& event);
    bool keyEvent (const safian_bitmap_gui_key_event_t& event);
    bool textInput (const char* utf8);
    void setFocus (bool focused);
    void requestDone (uint32_t requestId, uint32_t result, const char* path);
    bool showRemotePage (clap_id pageId);

    // for the tests of this module
    ui::SynthyBoyEditor& editorForTest() noexcept { return *editor; }
    NativeUiHost& hostForTest() noexcept { return *uiHost; }

private:
    class Root;
    void peerRepaint (const juce::Rectangle<int>& area) override;
    void peerTextInput (bool show, juce::TextInputTarget* target) override;
    void parameterGestureChanged (int index, bool starting) override;

    void allocateImage();
    void notifyRenderRequested (const juce::Rectangle<int>* area);
    void releasePointers();
    void updateCursor (double x, double y);
    void trackRemotePage();
    juce::ModifierKeys modifiersOf (uint32_t modifiers, uint32_t buttons) const noexcept;
    void sendMouse (juce::MouseInputSource::InputSourceType type, juce::Point<float> position, uint32_t modifiers, uint32_t buttons, float pressure);
    void fingerEvent (const safian_bitmap_gui_pointer_event_t& event);

    struct RuntimeLease
    {
        RuntimeLease();
        ~RuntimeLease();
    };
    RuntimeLease lease; // first member: the toolkit lives at least as long as everything below
    SynthyBoyClap& plugin;
    const safian_host_bitmap_gui_t* hostExt = nullptr;
    uint32_t createFlags = 0;
    int logicalWidth = 0, logicalHeight = 0;
    double scale = 1.0;

    std::unique_ptr<NativeUiHost> uiHost;
    std::unique_ptr<Root> root;
    std::unique_ptr<ui::SynthyBoyEditor> editor;
    BitmapPeer* peer = nullptr;

    juce::Image image;
    std::unique_ptr<juce::Image::BitmapData> mapped; // the pixels the frame hands out (a software image: no copy)
    juce::RectangleList<int> dirtyLogical;
    bool fullPending = true, renderRequested = false;
    uint64_t frameId = 0;
    std::vector<safian_bitmap_gui_rect_t> lastRects;
    bool lastFull = false;
    double activeUntilMs = 0.0;

    // Fingers. JUCE has touch input sources on iOS / Android / (touch) Windows only; to behave the same everywhere a finger is the primary button of the one mouse source and
    // two fingers together become the pinch and pan gestures a desktop trackpad makes (handleMagnifyGesture / smooth wheel), which the UI's components already take.
    struct Finger { uint32_t id; juce::Point<float> position; };
    std::vector<Finger> fingers;
    bool pairActive = false;
    float pairDistance = 0.0f;
    juce::Point<float> pairCentre;
    uint32_t cursorSent = 0xffffffffu;
    juce::Point<float> mousePosition;   // where the mouse / pen was last (for a press that arrives without a hover before it)
    bool hasMousePosition = false;
    bool textInputShown = false;

    std::string lastGestureKey;
    bool gesturePending = false;
    int lastTabs = -1;
    int64_t reportedRemotePage = -1;
};
} // namespace synthyboy::bitmap
