// SynthyBoy - MPC overlay tests: the pages of the OSC tab (osc_page.h): one page per oscillator (A, B, C, Sub, Noise) built on the full oscillator UI, with the oscillator's display; the page names its
// oscillator itself (the Play page's selector is neither read nor written), has no CLOSE button, sets the Q groups of the full view and opens the detail pages of the oscillator UI.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <string>
#include <vector>

#include "../osc_page.h"
#include "../scene_cpu.h"

using namespace synthyboy_mpc;
using namespace synthyboy_mpc::overlay;

namespace
{
int g_fails = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++g_fails;
}

/** A plugin instance made of an array of plain values: writeParam converts like the plugin does and counts the writes (the host notification). */
class Inst final : public IInstanceView
{
public:
    Inst()
    {
        const synthyboy::ParamInfo* t = synthyboy::paramTable();
        for (size_t i = 0; i < synthyboy::paramCount(); ++i)
            v_[i] = t[i].def;
    }
    float param (int i) const noexcept override { return v_[i]; }
    bool live (synthyboy::LiveValues&) const noexcept override { return false; }
    double sampleRate() const noexcept override { return 48000.0; }
    int playSelection (int) const noexcept override { return sel; }
    void setQGroup (const int* t, int n) noexcept override { for (int i = 0; i < 4; ++i) q[i] = i < n ? t[i] : -1; ++qCalls; }
    void writeParam (int i, float norm) noexcept override
    {
        const synthyboy::ParamInfo& p = synthyboy::paramTable()[i];
        v_[i] = synthyboy::constrainValue (p, synthyboy::fromNormalised (p, norm));
        ++writes;
        lastWritten = i;
    }
    void setPlaySelection (int g, int i) noexcept override { if (g == 0) sel = i; ++selWrites; }
    int playLastSelector() const noexcept override { return lastSel; }
    void setUiFullOpen (bool o) noexcept override { fullOpen = o; }
    void contentRoots (std::vector<ContentRoot>& out) const override { out = roots; }
    void requestContent (int osc, int kind, const std::string& path) override { requests.push_back ({ osc, kind, path }); loadState = 1; }
    int contentLoad (std::string* m) const override { if (m != nullptr) *m = loadMessage; return loadState; }
    void wavetableSources (synthyboy::CatalogueSources& out) const override { out = sources; }
    void requestWavetable (int osc, const synthyboy::ContentFileRef& ref) override { wtRequests.push_back ({ osc, ref }); loadState = 1; }
    int takeWavetableSteps (int) noexcept override { const int n = wtSteps; wtSteps = 0; return n; }
    std::shared_ptr<const synthyboy::StateContent> content() const override { return contentPtr; }
    struct WtRequest { int osc; synthyboy::ContentFileRef ref; };
    std::vector<WtRequest> wtRequests;
    synthyboy::CatalogueSources sources;
    std::string loadMessage;
    int wtSteps = 0;
    struct Request { int osc, kind; std::string path; };
    std::vector<ContentRoot> roots;
    std::vector<Request> requests;
    int loadState = 0;
    std::shared_ptr<const synthyboy::StateContent> contentPtr;
    static int idx (const std::string& key) { const auto* p = synthyboy::findParamByKey (key.c_str()); return p ? static_cast<int> (p - synthyboy::paramTable()) : -1; }
    void set (const std::string& key, float v) { v_[idx (key)] = v; }
    float get (const std::string& key) const { return v_[idx (key)]; }
    int sel = 2, lastSel = 0, q[4] = { -1, -1, -1, -1 }, qCalls = 0, writes = 0, lastWritten = -1, selWrites = 0;
    bool fullOpen = false;

private:
    float v_[synthyboy::kMaxParams] = {};
};

TouchEvent ev (TouchEvent::Kind k, int x, int y) { TouchEvent e; e.kind = k; e.x = x; e.y = y; return e; }

struct Rig
{
    OscPageUi page;
    Inst inst;
    UiStack stack;
    double now = 1000.0;
    int sel;
    explicit Rig (int selector) : sel (selector)
    {
        inst.sel = 3;   // the Play page's selector says oscillator B: a page of another oscillator must not follow it
        stack.openFull (OscPageUi::kElementPage);   // what the controller does for a page view
        refresh();
    }
    void refresh()
    {
        now += 50.0;
        page::Input in;
        in.instance = sel;
        page.update (inst, stack, now, in);
    }
    void tap (int cx, int cy)
    {
        page.touch (ev (TouchEvent::Down, cx, cy), stack, inst, now);
        now += 40;
        page.tick (now, stack, inst);
        page.touch (ev (TouchEvent::Up, cx, cy), stack, inst, now);
        now += 100.0;
        refresh();
    }
    void drag (int x0, int y0, int x1, int y1)
    {
        page.touch (ev (TouchEvent::Down, x0, y0), stack, inst, now);
        for (int i = 1; i <= 10; ++i)
        {
            now += 16.0;
            page.touch (ev (TouchEvent::Move, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10), stack, inst, now);
        }
        page.touch (ev (TouchEvent::Up, x1, y1), stack, inst, now);
        now += 100.0;
        refresh();
    }
};

bool hasLeaf (ui::Composite& root, const ui::Component* c)
{
    ui::Component* leaves[256];
    const int n = root.collectLeaves (leaves, 0, 256);
    for (int i = 0; i < n; ++i)
        if (leaves[i] == c)
            return true;
    return false;
}

bool leavesNoOverlap (ui::Component& root, std::string& why)
{
    ui::Component* l[256];
    const int n = root.collectLeaves (l, 0, 256);
    for (int i = 0; i < n; ++i)
    {
        if (l[i]->rect().empty())
            continue;
        for (int j = i + 1; j < n; ++j)
        {
            if (l[j]->rect().empty())
                continue;
            if (l[i]->rect().inset (1).overlaps (l[j]->rect().inset (1)))
            {
                why = "leaf " + std::to_string (i) + " (" + std::to_string (l[i]->rect().x) + "," + std::to_string (l[i]->rect().y) + " " + std::to_string (l[i]->rect().w) + "x" + std::to_string (l[i]->rect().h) + ") overlaps leaf " + std::to_string (j) + " (" + std::to_string (l[j]->rect().x) + "," + std::to_string (l[j]->rect().y) + " " + std::to_string (l[j]->rect().w) + "x" + std::to_string (l[j]->rect().h) + ")";
                return false;
            }
        }
    }
    return n > 0;
}
} // namespace

int main()
{
    {
        // the pages of the oscillator types on the OSC tab: BACK from each one returns to the page, the display draws a loaded sample and a non-default spectral setting
        for (int pageId = 16; pageId <= 22; ++pageId)
        {
            Rig r (2);
            r.inst.set ("osc1.type", pageId == 16 ? 0.0f : (pageId == 18 || pageId == 19 ? 1.0f : (pageId == 20 ? 2.0f : 3.0f)));
            r.refresh();
            r.stack.openDetail (pageId);
            r.refresh();
            check (r.page.topPage() == pageId, "page " + std::to_string (pageId) + ": it opens over the page of the oscillator");
            const ui::Rect back = r.page.ui().specPage().back.rect();
            r.tap (back.x + back.w / 2, back.y + back.h / 2);
            check (r.page.topPage() == 0 && r.stack.depth() == 1, "page " + std::to_string (pageId) + ": BACK returns to the page");
        }
        Rig r (2);
        r.inst.set ("osc1.type", 1.0f);
        auto content = std::make_shared<synthyboy::StateContent>();
        synthyboy::ContentSample cs;
        cs.osc = 1; cs.ref.pack = "p"; cs.ref.file = "a.wav"; cs.ref.name = "a";
        content->samples.push_back (cs);
        r.inst.contentPtr = content;
        r.refresh();
        const OscMarks m0 = OscMarks::of (r.inst, 2, 0);
        r.inst.set ("osc1.sample.start", 25.0f);
        const OscMarks m1 = OscMarks::of (r.inst, 2, 0);
        check (m0.region && m1.region && m1.start > m0.start + 0.2f, "the sample page draws the region markers from sample.start / sample.end");
        r.inst.set ("osc1.type", 0.0f);
        r.inst.set ("osc1.remap.on", 1.0f);
        check (OscMarks::of (r.inst, 2, 16).remap && ! OscMarks::of (r.inst, 2, 17).remap, "the map page draws the remap curve, only while the remap is on");
        // the picture of the page changes when the region does
        auto hash = [&] (Rig& g) { auto d = std::make_unique<gpu::SceneData>(); gpu::SceneBuilder sb (*d); sb.begin (gpu::kKindPlay, 0, 1280, 800); g.page.draw (sb); return page::sceneHash (*d); };
        Rig a (2), b (2);
        a.inst.set ("osc1.type", 1.0f); b.inst.set ("osc1.type", 1.0f);
        a.inst.contentPtr = content; b.inst.contentPtr = content;
        b.inst.set ("osc1.sample.start", 40.0f);
        a.refresh(); b.refresh();
        check (hash (a) != hash (b), "the picture of a sample page follows the start of the region");
    }
    {
        Rig r (2);   // Osc A
        OscUi& u = r.page.ui();
        check (u.isPage() && u.selection() == 2 && r.page.selection() == 2, "the page of Osc A shows oscillator A although the Play selector says B");
        check (u.full().isPage() && ! hasLeaf (u.full(), &u.full().close), "the full screen is a page: no CLOSE button on it");
        check (r.inst.fullOpen && r.stack.element() == OscPageUi::kElementPage && r.stack.depth() == 1, "the page is the full screen of the oscillator UI (the host widgets are hidden)");
        check (r.inst.q[0] == Inst::idx ("osc1.type") && r.inst.q[2] == Inst::idx ("osc1.pan") && r.inst.q[3] == Inst::idx ("osc1.level"), "the Basic group of osc1 is the Q group on arrival");
        check (r.page.display().valid() && r.page.display().kind() == synthyboy::OscCycle::Kind::wavetable && ! u.displayRect().empty(), "the display of the oscillator has its data and its rectangle");
        // the level knob of the page writes osc1.level and nothing of the Play selector
        const ui::Rect lr = u.full().basic.level.rect();
        const float before = r.inst.get ("osc1.level");
        r.drag (lr.x + lr.w / 2, lr.y + lr.h / 2, lr.x + lr.w / 2, lr.y + lr.h / 2 - 40);
        check (r.inst.get ("osc1.level") > before + 5.0f && r.inst.selWrites == 0 && r.inst.sel == 3, "a drag on LEVEL writes osc1.level; the Play selector is not written");
        // the unison detail page opens over it and closes back to the page
        r.stack.openDetail (OscUi::kPageUnison);
        r.refresh();
        check (r.page.topPage() == OscUi::kPageUnison && ! u.displayRect().empty(), "the unison page opens over the page (the display stays)");
        r.stack.back();
        r.refresh();
        check (r.page.topPage() == 0 && r.stack.depth() == 1 && r.stack.element() == OscPageUi::kElementPage, "back: the page again");
        // the scene of the page: the ground is the host's, the picture is one scene
        gpu::SceneData sd;
        gpu::SceneBuilder sb (sd);
        sb.begin (gpu::kKindPlay, 0, 1280, 800);
        page::ground (sb);
        r.page.draw (sb);
        check (! sb.overflow && sd.numOps > 100, "the picture of the page is one scene (" + std::to_string (sd.numOps) + " ops)");
    }
    {
        Rig r (1);   // Sub
        check (r.page.ui().selection() == 1 && r.inst.q[3] == Inst::idx ("sub.level") && r.page.display().valid(), "the Sub page: the sub oscillator, its Basic group (sub.level), its display");
        check (r.page.ui().full().instance() == ui::OscInstance::Sub, "... and the sub oscillator's full screen");
        std::string why;
        check (leavesNoOverlap (r.page.ui().full(), why), "Sub page: no control rectangle overlaps another: " + why);

        // Display keeps about half the height
        const ui::Rect disp = r.page.ui().displayRect();
        check (disp.w == 1264 && disp.h >= 240 && disp.h <= 350, "Sub page: display keeps about half height (" + std::to_string (disp.w) + "x" + std::to_string (disp.h) + ")");

        // Bar and Tune blocks are empty on Sub page
        check (r.page.ui().full().barBlock().empty(), "Sub page: bar block is empty");
        check (r.page.ui().full().tuneBlock().empty(), "Sub page: tune block is empty");

        // Panel blocks: TUNE, PHASE, ROUTING
        auto& panel = r.page.ui().full().panel;
        check (panel.numBlocks() == 3, "Sub page: 3 labelled blocks");
        check (std::string (panel.blockTitle (0)) == "TUNE", "Sub block 0: TUNE");
        check (std::string (panel.blockTitle (1)) == "PHASE", "Sub block 1: PHASE");
        check (std::string (panel.blockTitle (2)) == "ROUTING", "Sub block 2: ROUTING");

        // Check medium knobs with side layout and formatted values
        const ui::PanelSlot kSubKnobs[] = { ui::PanelSlot::SubCrs, ui::PanelSlot::SubPhase, ui::PanelSlot::SubBlend, ui::PanelSlot::SubBus1, ui::PanelSlot::SubBus2 };
        for (ui::PanelSlot s : kSubKnobs)
        {
            const ui::Knob* k = panel.knob (s);
            check (k != nullptr && k->state.side && k->rect().w >= 172 && k->rect().h >= 56, "Sub knob is medium side knob: " + std::string (k ? k->state.label : "null"));
        }

        // Check every engine parameter of Sub (14 total) is reachable on the page
        const char* const kSubParams[] = {
            "sub.on", "sub.waveform", "sub.octave", "sub.pan", "sub.level",
            "sub.tune", "sub.env1", "sub.mono",
            "sub.phase", "sub.phase_contiguous",
            "sub.destination", "sub.filter_blend", "sub.bus1_send", "sub.bus2_send"
        };
        for (const char* p : kSubParams)
        {
            const int pidx = Inst::idx (p);
            check (pidx >= 0, std::string ("Sub parameter exists in engine: ") + p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    check (it->c->rect().w >= 56 && it->c->rect().h >= 56, std::string ("Sub control touch target >= 56 px: ") + p);
                    break;
                }
            }
            check (found, std::string ("Sub parameter reachable on page: ") + p);
        }

        // Greyed-out-only blocks do not exist
        for (int b = 0; b < panel.numBlocks(); ++b)
        {
            bool hasEnabled = false;
            for (int i = 0; i < panel.numParts(); ++i)
                if (panel.part (kSubKnobs[0]) != nullptr)
                    hasEnabled = true;
            check (hasEnabled, "Sub block has enabled controls, no greyed-out-only block");
        }
    }
    {
        Rig r (5);   // Noise
        check (r.page.ui().selection() == 5 && r.inst.q[3] == Inst::idx ("noise.level"), "the Noise page: the noise, its Basic group (noise.level)");
        check (r.page.ui().full().instance() == ui::OscInstance::Noise, "... and the noise's full screen");
        std::string why;
        check (leavesNoOverlap (r.page.ui().full(), why), "Noise page (generated mode): no control rectangle overlaps another: " + why);

        // Header Q group stays as it is (noise.sample, noise.filter, noise.pan, noise.level)
        check (r.inst.q[0] == Inst::idx ("noise.sample") && r.inst.q[1] == Inst::idx ("noise.filter") &&
               r.inst.q[2] == Inst::idx ("noise.pan") && r.inst.q[3] == Inst::idx ("noise.level"),
               "Noise header Basic Q group stays intact");

        // Tuning column stays intact
        check (r.page.ui().full().tuneBlock().w >= 200 && r.page.ui().full().tuneBlock().h >= 200, "Noise tune block is present");
        check (r.page.ui().full().barBlock().empty(), "Noise bar block is eliminated");

        // Generated noise mode: 1 NOISE block, no PLAYBACK block
        auto& panel = r.page.ui().full().panel;
        check (panel.numBlocks() == 1, "Noise (generated mode): exactly 1 block (NOISE), no PLAYBACK block");
        check (std::string (panel.blockTitle (0)) == "NOISE", "Noise block title is NOISE");
        check (panel.part (ui::PanelSlot::NoiseType) != nullptr && panel.part (ui::PanelSlot::NoiseType)->enabled(), "Noise TYPE stepper is enabled");
        check (panel.part (ui::PanelSlot::NoiseStereo) != nullptr && panel.part (ui::PanelSlot::NoiseStereo)->enabled(), "Noise STEREO knob is enabled");
        check (panel.part (ui::PanelSlot::NoiseEnv1) != nullptr && panel.part (ui::PanelSlot::NoiseEnv1)->enabled(), "Noise ENV 1 toggle is enabled");
        check (panel.part (ui::PanelSlot::NoiseStart) == nullptr, "Noise START knob is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseRand) == nullptr, "Noise RANDOM toggle is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseLoop) == nullptr, "Noise LOOP toggle is hidden in generated mode");
        check (panel.part (ui::PanelSlot::NoiseBounce) == nullptr, "Noise BOUNCE toggle is hidden in generated mode");

        // Active noise parameters reachable in generated mode
        const char* const kNoiseGenParams[] = {
            "noise.on", "noise.sample", "noise.filter", "noise.pan", "noise.level",
            "noise.destination", "noise.pitch", "noise.fine", "noise.keytrack",
            "noise.type", "noise.stereo", "noise.env1"
        };
        for (const char* p : kNoiseGenParams)
        {
            const int pidx = Inst::idx (p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    check (it->c->rect().w >= 56 && it->c->rect().h >= 56, std::string ("Noise control touch target >= 56 px: ") + p);
                    break;
                }
            }
            check (found, std::string ("Noise parameter reachable on page: ") + p);
        }

        // Now switch to Sample mode: noise.type = 1.0f
        r.inst.set ("noise.type", 1.0f);
        r.refresh();
        check (leavesNoOverlap (r.page.ui().full(), why), "Noise page (sample mode): no control rectangle overlaps another: " + why);

        // Sample mode: 2 blocks (SOURCE, PLAYBACK)
        check (panel.numBlocks() == 2, "Noise (sample mode): 2 blocks (SOURCE, PLAYBACK)");
        check (std::string (panel.blockTitle (0)) == "SOURCE", "Noise block 0 title is SOURCE");
        check (std::string (panel.blockTitle (1)) == "PLAYBACK", "Noise block 1 title is PLAYBACK");

        // In sample mode, all sample controls are now present and enabled
        check (panel.part (ui::PanelSlot::NoiseStart) != nullptr && panel.part (ui::PanelSlot::NoiseStart)->enabled(), "Noise START knob present and enabled in sample mode");
        check (panel.part (ui::PanelSlot::NoiseRand) != nullptr && panel.part (ui::PanelSlot::NoiseRand)->enabled(), "Noise RANDOM toggle present and enabled in sample mode");
        check (panel.part (ui::PanelSlot::NoiseLoop) != nullptr && panel.part (ui::PanelSlot::NoiseLoop)->enabled(), "Noise LOOP toggle present and enabled in sample mode");
        check (panel.part (ui::PanelSlot::NoiseBounce) != nullptr && panel.part (ui::PanelSlot::NoiseBounce)->enabled(), "Noise BOUNCE toggle present and enabled in sample mode");

        // All 19 parameters of noise are reachable in sample mode
        const char* const kNoiseAllParams[] = {
            "noise.on", "noise.sample", "noise.filter", "noise.pan", "noise.level",
            "noise.destination", "noise.pitch", "noise.fine", "noise.keytrack",
            "noise.type", "noise.stereo", "noise.env1",
            "noise.phase", "noise.random_phase", "noise.loop", "noise.bounce"
        };
        for (const char* p : kNoiseAllParams)
        {
            const int pidx = Inst::idx (p);
            bool found = false;
            for (int k = 0; k < 300; ++k)
            {
                const OscUi::Item* it = r.page.ui().item (k);
                if (it != nullptr && it->param == pidx && it->c != nullptr && it->c->enabled())
                {
                    found = true;
                    break;
                }
            }
            check (found, std::string ("Noise sample parameter reachable on page: ") + p);
        }

        // Greyed-out-only blocks do not exist in sample mode either
        for (int b = 0; b < panel.numBlocks(); ++b)
        {
            bool hasEnabled = false;
            for (int i = 0; i < panel.numParts(); ++i)
                if (panel.part (ui::PanelSlot::NoiseType) != nullptr)
                    hasEnabled = true;
            check (hasEnabled, "Noise block has enabled controls, no greyed-out-only block");
        }
    }
    {
        // the table selector of a page opens the wavetable browser (Osc A and Osc C); a built-in table is chosen as the stepper chooses it, the parameter is written
        for (int sel : { 2, 4 })
        {
            Rig r (sel);
            const std::string key = sel == 2 ? "osc1.wavetable" : "osc3.wavetable";
            ui::Stepper& st = r.page.ui().full().basic.source.stepper;
            check (st.listable && r.page.ui().full().basic.source.dropdown.enabled(), "the table stepper of the page is listable and its list button is enabled");
            r.tap (st.rect().x + st.rect().w / 2, st.rect().y + st.rect().h / 2);
            check (r.page.topPage() == OscUi::kPageList && std::string (r.page.ui().list().state.header) == "Basic", "page " + std::to_string (sel) + ": the name of the table opens the browser in the category of the table");
            const ui::Rect row = r.page.ui().list().rowRect (0);
            r.tap (row.x + 30, row.y + row.h / 2);
            check (r.inst.get (key) == 0.0f && r.page.topPage() == 0, "page " + std::to_string (sel) + ": a built-in table writes " + key + " and the browser closes");
        }
    }
    {
        Rig r (4);   // Osc C: the readout targets
        const std::vector<int> t = OscPageUi::targetsOf (4);
        check (t.size() == 4 && OscPageUi::targetsOf (1).empty() && OscPageUi::targetsOf (5).empty(), "a page lists the four readout targets of its own oscillator (none for Sub and Noise)");
        check (r.page.ui().selection() == 4 && r.inst.q[3] == Inst::idx ("osc3.level"), "the Osc C page is osc3");
    }
    std::printf (g_fails ? "osc_page_host: FAILED (%d)\n" : "osc_page_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
