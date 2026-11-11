// SynthyBoy - bitmap GUI: one GUI of one plug-in instance (see BitmapSession.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BitmapSession.h"

#include <algorithm>
#include <cmath>

#include "JuceRuntime.h"
#include "synthyboy/kick_pages.h"
#include "ui/Panel.h"
#include "ui/PanelHost.h"

#if JUCE_BIG_ENDIAN
 #error "the bitmap GUI frame format is BGRA8888 little endian (bitmap_gui_ext.h); a big endian target needs a byte swap on output"
#endif

namespace synthyboy::bitmap
{
BitmapSession::RuntimeLease::RuntimeLease() { JuceRuntime::acquire(); }
BitmapSession::RuntimeLease::~RuntimeLease() { JuceRuntime::release(); }

/** The component the editor lives in: on the desktop with a BitmapPeer. */
class BitmapSession::Root final : public juce::Component
{
public:
    explicit Root (BitmapPeer::Client& peerClient) : client (peerClient)
    {
        setOpaque (true);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    BitmapPeer* bitmapPeer() const noexcept { return created; }

protected:
    juce::ComponentPeer* createNewPeer (int styleFlags, void*) override
    {
        created = new BitmapPeer (*this, styleFlags, client);
        return created;
    }

private:
    BitmapPeer::Client& client;
    BitmapPeer* created = nullptr;
};

BitmapSession::BitmapSession (SynthyBoyClap& owner, uint32_t width, uint32_t height, double initialScale, uint32_t flags)
    : plugin (owner), createFlags (flags)
{
    if (plugin.host != nullptr && plugin.host->get_extension != nullptr)
        hostExt = static_cast<const safian_host_bitmap_gui_t*> (plugin.host->get_extension (plugin.host, SAFIAN_EXT_BITMAP_GUI_HOST));

    scale = std::clamp (initialScale > 0.0 ? initialScale : 1.0, kMinScale, kMaxScale);
    logicalWidth = static_cast<int> (std::clamp<uint32_t> (width != 0 ? width : kDefaultWidth, ui::SynthyBoyEditor::kMinWidth, ui::SynthyBoyEditor::kMaxWidth));
    logicalHeight = static_cast<int> (std::clamp<uint32_t> (height != 0 ? height : kDefaultHeight, ui::SynthyBoyEditor::kMinHeight, ui::SynthyBoyEditor::kMaxHeight));

    uiHost = std::make_unique<NativeUiHost> (plugin, hostExt);
    root = std::make_unique<Root> (static_cast<BitmapPeer::Client&> (*this));
    root->setBounds (0, 0, logicalWidth, logicalHeight);
    editor = std::make_unique<ui::SynthyBoyEditor> (*uiHost);
    root->addAndMakeVisible (*editor);
    editor->setBounds (root->getLocalBounds());
    root->addToDesktop (0);
    root->setVisible (true);
    peer = root->bitmapPeer();
    uiHost->parameterStore().addListener (this);
    allocateImage();
}

BitmapSession::~BitmapSession()
{
    JuceRuntime::enter();
    uiHost->parameterStore().removeListener (this);
    releasePointers();
    if (textInputShown && hostExt != nullptr && hostExt->request_text_input != nullptr)
        hostExt->request_text_input (plugin.host, false, nullptr, SAFIAN_BITMAP_GUI_TEXT_KIND_TEXT, nullptr);
    textInputShown = false;
    peer = nullptr;
    editor.reset();
    if (root != nullptr)
        root->removeFromDesktop(); // deletes the peer while this object is still whole
    root.reset();
    // late messages of the toolkit that belong to the editor (deleted-later call-outs) are dispatched before the host goes: they would find a dead store otherwise
    if (! (createFlags & SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP))
        JuceRuntime::pump();
    uiHost.reset();
}

// ---- size, image -------------------------------------------------------------------------------------------------------------------------------

void BitmapSession::allocateImage()
{
    const int deviceWidth = std::max (1, static_cast<int> (std::lround (logicalWidth * scale)));
    const int deviceHeight = std::max (1, static_cast<int> (std::lround (logicalHeight * scale)));
    // a software image: one renderer on every platform, so the pixels are the same on every platform (no CoreGraphics, no Direct2D, no X11)
    image = juce::Image (juce::Image::ARGB, deviceWidth, deviceHeight, true, juce::SoftwareImageType());
    mapped = std::make_unique<juce::Image::BitmapData> (image, juce::Image::BitmapData::readOnly);
    dirtyLogical.clear();
    fullPending = true;
}

bool BitmapSession::setSize (uint32_t width, uint32_t height)
{
    JuceRuntime::enter();
    const int w = static_cast<int> (std::clamp<uint32_t> (width, ui::SynthyBoyEditor::kMinWidth, ui::SynthyBoyEditor::kMaxWidth));
    const int h = static_cast<int> (std::clamp<uint32_t> (height, ui::SynthyBoyEditor::kMinHeight, ui::SynthyBoyEditor::kMaxHeight));
    if (w == logicalWidth && h == logicalHeight)
        return true;
    logicalWidth = w;
    logicalHeight = h;
    root->setSize (w, h);
    editor->setBounds (root->getLocalBounds());
    allocateImage();
    notifyRenderRequested (nullptr);
    return true;
}

bool BitmapSession::setScale (double newScale)
{
    JuceRuntime::enter();
    const double clamped = std::clamp (newScale > 0.0 ? newScale : 1.0, kMinScale, kMaxScale);
    if (juce::exactlyEqual (clamped, scale))
        return true;
    scale = clamped;
    allocateImage();
    notifyRenderRequested (nullptr);
    return true;
}

// ---- frames ------------------------------------------------------------------------------------------------------------------------------------

void BitmapSession::peerRepaint (const juce::Rectangle<int>& area)
{
    if (! fullPending)
    {
        dirtyLogical.add (area);
        if (static_cast<uint32_t> (dirtyLogical.getNumRectangles()) > 4 * kMaxDirtyRects)
            dirtyLogical = juce::RectangleList<int> (dirtyLogical.getBounds()); // many small pieces: one box costs less than the bookkeeping
    }
    notifyRenderRequested (&area);
}

void BitmapSession::notifyRenderRequested (const juce::Rectangle<int>* area)
{
    if (renderRequested)
        return;
    renderRequested = true;
    if (hostExt != nullptr && hostExt->request_render != nullptr)
    {
        if (area != nullptr)
        {
            const safian_bitmap_gui_rect_t rect { area->getX(), area->getY(), static_cast<uint32_t> (area->getWidth()), static_cast<uint32_t> (area->getHeight()) };
            hostExt->request_render (plugin.host, &rect);
        }
        else
            hostExt->request_render (plugin.host, nullptr);
    }
}

bool BitmapSession::tick()
{
    JuceRuntime::enter();
    if (! (createFlags & SAFIAN_BITMAP_GUI_CREATE_HOST_RUNS_EVENT_LOOP))
        JuceRuntime::pump();
    uiHost->pullChangesFromHost();
    trackRemotePage();
    return isDirty();
}

bool BitmapSession::render (safian_bitmap_gui_frame_t& frame)
{
    tick();
    const auto now = static_cast<double> (juce::Time::getMillisecondCounterHiRes());

    lastRects.clear();
    lastFull = false;
    if (isDirty())
    {
        const juce::Rectangle<int> deviceBounds = image.getBounds();
        const float sx = static_cast<float> (image.getWidth()) / static_cast<float> (logicalWidth);
        const float sy = static_cast<float> (image.getHeight()) / static_cast<float> (logicalHeight);

        juce::RectangleList<int> deviceRects;
        if (fullPending)
        {
            deviceRects.add (deviceBounds);
            lastFull = true;
        }
        else
        {
            for (const auto& r : dirtyLogical)
                deviceRects.add (r.toFloat().transformedBy (juce::AffineTransform::scale (sx, sy)).getSmallestIntegerContainer().getIntersection (deviceBounds));
            if (static_cast<uint32_t> (deviceRects.getNumRectangles()) > kMaxDirtyRects)
                deviceRects = juce::RectangleList<int> (deviceRects.getBounds());
            lastFull = deviceRects.getNumRectangles() == 1 && deviceRects.getRectangle (0) == deviceBounds;
        }
        dirtyLogical.clear();
        fullPending = false;
        renderRequested = false;

        if (! deviceRects.isEmpty())
        {
            auto context = image.getPixelData()->createLowLevelContext();
            context->clipToRectangleList (deviceRects);
            juce::Graphics g (*context);
            g.addTransform (juce::AffineTransform::scale (sx, sy));
            root->paintEntireComponent (g, true);

            for (const auto& r : deviceRects)
                lastRects.push_back ({ r.getX(), r.getY(), static_cast<uint32_t> (r.getWidth()), static_cast<uint32_t> (r.getHeight()) });
            ++frameId;
            activeUntilMs = now + 250.0;
        }
    }

    frame.pixels = mapped->data;
    frame.width = static_cast<uint32_t> (image.getWidth());
    frame.height = static_cast<uint32_t> (image.getHeight());
    frame.stride_bytes = static_cast<uint32_t> (mapped->lineStride);
    frame.pixel_format = SAFIAN_BITMAP_GUI_PIXEL_BGRA8888_PREMUL;
    frame.frame_id = frameId;
    frame.dirty_count = static_cast<uint32_t> (lastRects.size());
    frame.dirty = lastRects.empty() ? nullptr : lastRects.data();
    frame.flags = (lastFull ? SAFIAN_BITMAP_GUI_FRAME_FULL : 0u);
    frame.next_frame_ms = 0;
    if (now < activeUntilMs)
    {
        frame.flags |= SAFIAN_BITMAP_GUI_FRAME_NEEDS_ANOTHER;
        frame.next_frame_ms = 16;
    }
    return true;
}

// ---- pointers, wheel ----------------------------------------------------------------------------------------------------------------------------

juce::ModifierKeys BitmapSession::modifiersOf (uint32_t modifiers, uint32_t buttons) const noexcept
{
    int flags = 0;
    if (modifiers & SAFIAN_BITMAP_GUI_MOD_SHIFT) flags |= juce::ModifierKeys::shiftModifier;
    if (modifiers & SAFIAN_BITMAP_GUI_MOD_CTRL) flags |= juce::ModifierKeys::ctrlModifier;
    if (modifiers & SAFIAN_BITMAP_GUI_MOD_ALT) flags |= juce::ModifierKeys::altModifier;
    if (modifiers & SAFIAN_BITMAP_GUI_MOD_META) flags |= juce::ModifierKeys::commandModifier;
    if (buttons & SAFIAN_BITMAP_GUI_BUTTON_PRIMARY) flags |= juce::ModifierKeys::leftButtonModifier;
    if (buttons & SAFIAN_BITMAP_GUI_BUTTON_SECONDARY) flags |= juce::ModifierKeys::rightButtonModifier;
    if (buttons & SAFIAN_BITMAP_GUI_BUTTON_MIDDLE) flags |= juce::ModifierKeys::middleButtonModifier;
    return juce::ModifierKeys (flags);
}

void BitmapSession::sendMouse (juce::MouseInputSource::InputSourceType type, juce::Point<float> position, uint32_t modifiers, uint32_t buttons, float pressure)
{
    peer->handleMouseEvent (type, position, modifiersOf (modifiers, buttons), pressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis(), {}, 0);
}

void BitmapSession::releasePointers()
{
    if (peer == nullptr)
        return;
    if (! fingers.empty() && ! pairActive)
        sendMouse (juce::MouseInputSource::InputSourceType::mouse, fingers.front().position, 0, 0, juce::MouseInputSource::defaultPressure);
    if (! fingers.empty())
        sendMouse (juce::MouseInputSource::InputSourceType::mouse, juce::MouseInputSource::offscreenMousePos, 0, 0, juce::MouseInputSource::defaultPressure);
    fingers.clear();
    pairActive = false;
}

/** A finger: the first one is the mouse with its primary button down; the second one turns the pair into pinch / pan gestures. */
void BitmapSession::fingerEvent (const safian_bitmap_gui_pointer_event_t& e)
{
    using Source = juce::MouseInputSource::InputSourceType;
    const juce::Point<float> position (static_cast<float> (e.x), static_cast<float> (e.y));
    const auto it = std::find_if (fingers.begin(), fingers.end(), [&] (const Finger& f) { return f.id == e.pointer_id; });
    const float pressure = e.pressure > 0.0f ? e.pressure : 1.0f;

    switch (e.type)
    {
        case SAFIAN_BITMAP_GUI_POINTER_DOWN:
            if (it != fingers.end())
                return; // a second DOWN of a finger that is down already
            fingers.push_back ({ e.pointer_id, position });
            if (fingers.size() == 1)
            {
                // the finger arrives, then presses: components (and pop-up menus, which only let a release pick an entry after they saw the pointer move) get the move first
                sendMouse (Source::mouse, position, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
                sendMouse (Source::mouse, position, e.modifiers, SAFIAN_BITMAP_GUI_BUTTON_PRIMARY, pressure);
            }
            else if (fingers.size() == 2)
            {
                // the first finger stops acting as a mouse (its drag ends where it is) and the two become a pair
                sendMouse (Source::mouse, fingers[0].position, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
                sendMouse (Source::mouse, juce::MouseInputSource::offscreenMousePos, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
                pairActive = true;
                pairDistance = fingers[0].position.getDistanceFrom (fingers[1].position);
                pairCentre = (fingers[0].position + fingers[1].position) * 0.5f;
            }
            break;

        case SAFIAN_BITMAP_GUI_POINTER_MOVE:
            if (it == fingers.end())
                return;
            it->position = position;
            if (! pairActive)
                sendMouse (Source::mouse, position, e.modifiers, SAFIAN_BITMAP_GUI_BUTTON_PRIMARY, pressure);
            else if (fingers.size() >= 2 && (it == fingers.begin() || it == fingers.begin() + 1))
            {
                const float distance = fingers[0].position.getDistanceFrom (fingers[1].position);
                const auto centre = (fingers[0].position + fingers[1].position) * 0.5f;
                const auto delta = centre - pairCentre;
                const auto time = juce::Time::currentTimeMillis();
                // one gesture per move: the one the fingers did more of (the spreading of the pair against the travel of its middle)
                if (pairDistance > 1.0f && distance > 1.0f && std::abs (distance - pairDistance) > delta.getDistanceFromOrigin())
                    peer->handleMagnifyGesture (Source::mouse, centre, time, distance / pairDistance, 0);
                else if (delta.getDistanceFromOrigin() > 0.0f)
                {
                    juce::MouseWheelDetails pan;
                    pan.deltaX = delta.x / 200.0f; // the components read a pan of the wheel as deltaX / deltaY * 200 pixels
                    pan.deltaY = delta.y / 200.0f;
                    pan.isReversed = false;
                    pan.isSmooth = true;
                    pan.isInertial = false;
                    peer->handleMouseWheel (Source::mouse, centre, time, pan, 0);
                }
                pairDistance = distance;
                pairCentre = centre;
            }
            break;

        case SAFIAN_BITMAP_GUI_POINTER_UP:
        case SAFIAN_BITMAP_GUI_POINTER_CANCEL:
            if (it == fingers.end())
                return;
            if (! pairActive)
            {
                sendMouse (Source::mouse, position, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
                sendMouse (Source::mouse, juce::MouseInputSource::offscreenMousePos, e.modifiers, 0, juce::MouseInputSource::defaultPressure); // a finger that lifts leaves: no hover
            }
            fingers.erase (it);
            if (fingers.empty())
                pairActive = false; // the gesture ends with the last finger; a finger that stays down does not start a drag
            break;

        default:
            break;
    }
}

bool BitmapSession::pointerEvent (const safian_bitmap_gui_pointer_event_t& e)
{
    JuceRuntime::enter();
    if (peer == nullptr)
        return false;

    using Source = juce::MouseInputSource::InputSourceType;
    const juce::Point<float> position (static_cast<float> (e.x), static_cast<float> (e.y));
    const bool inside = peer->getBounds().withZeroOrigin().toFloat().contains (position);

    if (e.kind == SAFIAN_BITMAP_GUI_POINTER_KIND_TOUCH)
    {
        fingerEvent (e);
        return inside;
    }

    const Source type = e.kind == SAFIAN_BITMAP_GUI_POINTER_KIND_PEN ? Source::pen : Source::mouse;
    if (e.type == SAFIAN_BITMAP_GUI_POINTER_DOWN && ! (hasMousePosition && mousePosition == position))
        sendMouse (type, position, e.modifiers, 0, juce::MouseInputSource::defaultPressure); // a press where the pointer was not: it arrives first (see fingerEvent)
    mousePosition = position;
    hasMousePosition = e.type != SAFIAN_BITMAP_GUI_POINTER_LEAVE;
    const float pressure = e.pressure > 0.0f ? e.pressure : (e.buttons != 0 ? 1.0f : juce::MouseInputSource::defaultPressure);
    switch (e.type)
    {
        case SAFIAN_BITMAP_GUI_POINTER_DOWN:
        case SAFIAN_BITMAP_GUI_POINTER_MOVE:
            sendMouse (type, position, e.modifiers, e.buttons != 0 ? e.buttons : static_cast<uint32_t> (SAFIAN_BITMAP_GUI_BUTTON_PRIMARY), pressure);
            break;
        case SAFIAN_BITMAP_GUI_POINTER_HOVER:
            sendMouse (type, position, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
            break;
        case SAFIAN_BITMAP_GUI_POINTER_UP:
        case SAFIAN_BITMAP_GUI_POINTER_CANCEL:
            sendMouse (type, position, e.modifiers, e.type == SAFIAN_BITMAP_GUI_POINTER_UP ? (e.buttons & ~static_cast<uint32_t> (SAFIAN_BITMAP_GUI_BUTTON_PRIMARY)) : 0u,
                         juce::MouseInputSource::defaultPressure);
            break;
        case SAFIAN_BITMAP_GUI_POINTER_LEAVE:
            sendMouse (type, juce::MouseInputSource::offscreenMousePos, e.modifiers, 0, juce::MouseInputSource::defaultPressure);
            break;
        default:
            return false;
    }
    updateCursor (e.x, e.y);
    return inside;
}

void BitmapSession::updateCursor (double x, double y)
{
    if (hostExt == nullptr || hostExt->set_cursor == nullptr)
        return;
    juce::MouseCursor cursor;
    if (auto* component = root->getComponentAt (juce::Point<int> (static_cast<int> (x), static_cast<int> (y))))
        cursor = component->getMouseCursor();
    using Type = juce::MouseCursor::StandardCursorType;
    uint32_t shape = SAFIAN_BITMAP_GUI_CURSOR_DEFAULT;
    if (cursor == Type::NoCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_HIDDEN;
    else if (cursor == Type::PointingHandCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_POINTER;
    else if (cursor == Type::IBeamCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_TEXT;
    else if (cursor == Type::CrosshairCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_CROSSHAIR;
    else if (cursor == Type::LeftRightResizeCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_RESIZE_H;
    else if (cursor == Type::UpDownResizeCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_RESIZE_V;
    else if (cursor == Type::UpDownLeftRightResizeCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_RESIZE_ALL;
    else if (cursor == Type::DraggingHandCursor) shape = SAFIAN_BITMAP_GUI_CURSOR_GRABBING;
    if (shape != cursorSent)
    {
        cursorSent = shape;
        hostExt->set_cursor (plugin.host, shape);
    }
}

bool BitmapSession::wheelEvent (const safian_bitmap_gui_wheel_event_t& e)
{
    JuceRuntime::enter();
    if (peer == nullptr)
        return false;
    constexpr float kNotch = 120.0f / 256.0f; // what the Windows peer of JUCE reports for one detent
    juce::MouseWheelDetails details;
    details.deltaX = static_cast<float> (e.delta_x) * kNotch;
    details.deltaY = static_cast<float> (e.delta_y) * kNotch;
    details.isReversed = false;
    details.isSmooth = (e.flags & SAFIAN_BITMAP_GUI_WHEEL_PRECISE) != 0;
    details.isInertial = false;
    const juce::Point<float> position (static_cast<float> (e.x), static_cast<float> (e.y));
    peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, position, juce::Time::currentTimeMillis(), details, 0);
    return peer->getBounds().withZeroOrigin().toFloat().contains (position);
}

// ---- keys, text, focus ---------------------------------------------------------------------------------------------------------------------------

namespace
{
int juceKeyCode (uint32_t code) noexcept
{
    switch (code)
    {
        case SAFIAN_BITMAP_GUI_KEY_BACKSPACE: return juce::KeyPress::backspaceKey;
        case SAFIAN_BITMAP_GUI_KEY_TAB: return juce::KeyPress::tabKey;
        case SAFIAN_BITMAP_GUI_KEY_ENTER: return juce::KeyPress::returnKey;
        case SAFIAN_BITMAP_GUI_KEY_ESCAPE: return juce::KeyPress::escapeKey;
        case SAFIAN_BITMAP_GUI_KEY_DELETE: return juce::KeyPress::deleteKey;
        case SAFIAN_BITMAP_GUI_KEY_INSERT: return juce::KeyPress::insertKey;
        case SAFIAN_BITMAP_GUI_KEY_HOME: return juce::KeyPress::homeKey;
        case SAFIAN_BITMAP_GUI_KEY_END: return juce::KeyPress::endKey;
        case SAFIAN_BITMAP_GUI_KEY_PAGE_UP: return juce::KeyPress::pageUpKey;
        case SAFIAN_BITMAP_GUI_KEY_PAGE_DOWN: return juce::KeyPress::pageDownKey;
        case SAFIAN_BITMAP_GUI_KEY_LEFT: return juce::KeyPress::leftKey;
        case SAFIAN_BITMAP_GUI_KEY_RIGHT: return juce::KeyPress::rightKey;
        case SAFIAN_BITMAP_GUI_KEY_UP: return juce::KeyPress::upKey;
        case SAFIAN_BITMAP_GUI_KEY_DOWN: return juce::KeyPress::downKey;
        default: break;
    }
    if (code >= SAFIAN_BITMAP_GUI_KEY_F1 && code < SAFIAN_BITMAP_GUI_KEY_F1 + 12)
        return juce::KeyPress::F1Key + static_cast<int> (code - SAFIAN_BITMAP_GUI_KEY_F1);
    if (code >= 'a' && code <= 'z')
        return static_cast<int> (code - 'a' + 'A'); // JUCE names the letter keys by their upper case letter
    return code < 0x110000u ? static_cast<int> (code) : 0;
}
} // namespace

bool BitmapSession::keyEvent (const safian_bitmap_gui_key_event_t& e)
{
    JuceRuntime::enter();
    if (peer == nullptr)
        return false;
    // exactly one GUI of the module owns the keyboard: the one the host focused. A GUI that was focused earlier left its focused component behind; take it back here.
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || ! root->isParentOf (focused))
        peer->setHostFocus (true);

    const int code = juceKeyCode (e.key_code);
    if (code == 0)
        return false;
    if (e.type == SAFIAN_BITMAP_GUI_KEY_UP_EVENT)
        return peer->handleKeyUpOrDown (false);

    juce::juce_wchar textCharacter = 0;
    if (e.text != nullptr && e.text[0] != '\0')
        textCharacter = juce::CharPointer_UTF8 (e.text).getAndAdvance();
    const juce::KeyPress press (code, modifiersOf (e.modifiers, 0), textCharacter);
    peer->handleKeyUpOrDown (true);
    return peer->handleKeyPress (press);
}

bool BitmapSession::textInput (const char* utf8)
{
    JuceRuntime::enter();
    if (peer == nullptr || utf8 == nullptr)
        return false;
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || ! root->isParentOf (focused))
        return false;
    bool used = false;
    for (juce::CharPointer_UTF8 p (utf8); ! p.isEmpty();)
    {
        const juce::juce_wchar c = p.getAndAdvance();
        used = peer->handleKeyPress (juce::KeyPress (static_cast<int> (c), juce::ModifierKeys(), c)) || used;
    }
    return used;
}

void BitmapSession::setFocus (bool focused)
{
    JuceRuntime::enter();
    if (peer == nullptr)
        return;
    if (! focused)
        releasePointers();
    peer->setHostFocus (focused);
    if (! focused)
        peerTextInput (false, nullptr);
}

void BitmapSession::peerTextInput (bool show, juce::TextInputTarget* target)
{
    if (hostExt == nullptr || hostExt->request_text_input == nullptr)
        return;
    if (show && target != nullptr)
    {
        safian_bitmap_gui_rect_t rect { 0, 0, 0, 0 };
        if (auto* component = dynamic_cast<juce::Component*> (target))
        {
            const auto area = root->getLocalArea (component, component->getLocalBounds());
            rect = { area.getX(), area.getY(), static_cast<uint32_t> (area.getWidth()), static_cast<uint32_t> (area.getHeight()) };
        }
        const auto text = target->getTextInRange (juce::Range<int> (0, 4096));
        const auto keyboard = target->getKeyboardType();
        const uint32_t kind = (keyboard == juce::TextInputTarget::numericKeyboard || keyboard == juce::TextInputTarget::decimalKeyboard) ? SAFIAN_BITMAP_GUI_TEXT_KIND_NUMBER
                                                                                                                                         : SAFIAN_BITMAP_GUI_TEXT_KIND_TEXT;
        textInputShown = true;
        hostExt->request_text_input (plugin.host, true, &rect, kind, text.toRawUTF8());
    }
    else if (textInputShown)
    {
        textInputShown = false;
        hostExt->request_text_input (plugin.host, false, nullptr, SAFIAN_BITMAP_GUI_TEXT_KIND_TEXT, nullptr);
    }
}

// ---- answers of the host, remote controls ----------------------------------------------------------------------------------------------------

void BitmapSession::requestDone (uint32_t requestId, uint32_t result, const char* path)
{
    JuceRuntime::enter();
    uiHost->requestDone (requestId, result, path);
    if (hostExt != nullptr && hostExt->request_tick != nullptr)
        hostExt->request_tick (plugin.host, 0); // the answer may have queued work for the toolkit
}

void BitmapSession::parameterGestureChanged (int index, bool starting)
{
    if (starting && index >= 0 && static_cast<size_t> (index) < paramCount())
    {
        lastGestureKey = paramTable()[index].key;
        gesturePending = true;
    }
}

namespace
{
/** The remote-controls page (index in kickPageTable ()) that holds `key`; -1 when none does. */
int remotePageOfKey (const std::string& key)
{
    const Page* pages = kickPageTable();
    const size_t count = kickPageCount();
    for (size_t i = 0; i < count; ++i)
        for (size_t k = 0; k < pages[i].count; ++k)
            if (pages[i].keys[k] != nullptr && key == pages[i].keys[k])
                return static_cast<int> (i);
    for (size_t i = 0; i < count; ++i)
        for (size_t k = 0; k < pages[i].moreCount; ++k)
            if (pages[i].moreKeys[k] != nullptr && key == pages[i].moreKeys[k])
                return static_cast<int> (i);
    return -1;
}
} // namespace

void BitmapSession::trackRemotePage()
{
    if (hostExt == nullptr || hostExt->set_active_remote_page == nullptr)
        return;
    int page = -1;
    const int tab = static_cast<int> (editor->pageTabs().selected());
    if (tab != lastTabs)
    {
        // the user chose a tab: the first panel of that tab names the page
        lastTabs = tab;
        const auto& entries = ui::PanelRegistry::instance().entries();
        const auto& panels = editor->panelHost().allPanels();
        for (size_t i = 0; i < entries.size() && i < panels.size() && page < 0; ++i)
            if (static_cast<int> (entries[i].page) == tab)
                for (const auto& key : panels[i]->keys())
                    if ((page = remotePageOfKey (key)) >= 0)
                        break;
        gesturePending = false;
    }
    else if (gesturePending)
    {
        gesturePending = false;
        page = remotePageOfKey (lastGestureKey);
    }
    if (page >= 0 && page != reportedRemotePage)
    {
        reportedRemotePage = page;
        hostExt->set_active_remote_page (plugin.host, static_cast<clap_id> (page));
    }
}

bool BitmapSession::showRemotePage (clap_id pageId)
{
    JuceRuntime::enter();
    if (pageId >= kickPageCount())
        return false;
    const Page& page = kickPageTable()[pageId];
    const auto& entries = ui::PanelRegistry::instance().entries();
    const auto& panels = editor->panelHost().allPanels();
    for (size_t k = 0; k < page.count; ++k)
    {
        if (page.keys[k] == nullptr)
            continue;
        for (size_t i = 0; i < entries.size() && i < panels.size(); ++i)
        {
            const auto keys = panels[i]->keys();
            if (std::find (keys.begin(), keys.end(), std::string (page.keys[k])) != keys.end())
            {
                editor->pageTabs().select (entries[i].page, true);
                lastTabs = static_cast<int> (entries[i].page);
                reportedRemotePage = static_cast<int64_t> (pageId); // the host moved the bank itself: no echo
                return true;
            }
        }
    }
    return false;
}
} // namespace synthyboy::bitmap
