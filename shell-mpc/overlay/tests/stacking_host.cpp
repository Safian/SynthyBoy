// SynthyBoy - MPC overlay tests: the stacking of our plane against the host's pointer plane: the rule (discovery.h chooseZpos, stackedAbove, pointerStacking) over the tables of the two devices and the
// corner cases, and the guard (stacking.h) over a fake driver: set when taken, put back when released, again after a loss, a failing write, the Key 61 without a zpos property, the status text.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <map>
#include <string>

#include "../stacking.h"

using namespace synthyboy_mpc::overlay;

static int g_fails = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++g_fails;
}

struct FakeDriver final : IZposDriver
{
    std::map<uint32_t, PlaneZpos> planes;
    std::vector<std::pair<uint32_t, uint64_t>> writes;
    bool failWrites = false;
    bool readZpos (uint32_t plane, PlaneZpos& out) override
    {
        auto it = planes.find (plane);
        if (it == planes.end())
            return false;
        out = it->second;
        return true;
    }
    bool writeZpos (uint32_t plane, uint64_t value, std::string& error) override
    {
        if (failWrites)
        {
            error = "Permission denied";
            return false;
        }
        writes.push_back ({ plane, value });
        planes[plane].value = value;
        return true;
    }
    void add (uint32_t id, uint64_t value, bool mut = true, uint64_t lo = 0, uint64_t hi = 7) { PlaneZpos z; z.has = true; z.mutableProp = mut; z.min = lo; z.max = hi; z.value = value; planes[id] = z; }
    void addNone (uint32_t id) { planes[id] = PlaneZpos(); }
};

static std::string line (const std::string& text, const std::string& key)
{
    const size_t p = text.find (key + ": ");
    if (p == std::string::npos) return "-";
    const size_t e = text.find ('\n', p);
    return text.substr (p + key.size() + 2, e - p - key.size() - 2);
}

int main()
{
    const StackPlane host { 34, 0 };
    // ---- the rule ----
    {
        ZposChoice c = chooseZpos (46, true, 0, 7, 2, &host, { { 40, 1 } });
        check (c.possible && c.value == 0, "the XL table (host 34 at 0, pointer 40 at 1, ours 46 at 2): 0");
        check (stackedAbove (0, 46, 0, 34) && ! stackedAbove (0, 34, 0, 46) && stackedAbove (1, 40, 0, 46) && ! stackedAbove (0, 46, 1, 40), "... the order it relies on: equal zpos, the higher id above; zpos first");
        c = chooseZpos (20, true, 0, 7, 5, &host, { { 40, 3 } });
        check (c.possible && c.value == 1, "our id is lower than the primary's (20 < 34): primary zpos + 1 (value " + std::to_string (c.value) + ")");
        c = chooseZpos (20, true, 0, 7, 2, &host, { { 40, 3 } });
        check (c.possible && c.value == 1, "... a current value that is already in order is not kept: the lowest one above the primary (1), the pointer may be hidden now and come later");
        c = chooseZpos (20, true, 0, 7, 5, &host, { { 40, 1 } });
        check (c.possible && c.value == 1, "... the pointer at 1 with the higher id (40 > 20): 1 is still below it (ties by id)");
        c = chooseZpos (50, true, 0, 7, 5, &host, { { 40, 1 } });
        check (c.possible && c.value == 0, "our id higher than both: 0 (equal zpos 1 would put us over the pointer, 0 is below it)");
        c = chooseZpos (50, true, 0, 7, 5, &host, { { 40, 0 } });
        check (! c.possible, "the pointer at 0 with a lower id than ours (host 34, pointer 40, ours 50): no value fits between them (ours at 0 is above the pointer)");
        c = chooseZpos (20, true, 0, 7, 5, &host, { { 10, 1 } });
        check (! c.possible, "our id lower than the primary's and the pointer's at the same zpos with a still lower id: 1 puts us over it: none");
        c = chooseZpos (20, true, 0, 7, 0, &host, {});
        check (c.possible && c.value == 1, "no other plane in the application, ours under the primary: the lowest value above it (1)");
        c = chooseZpos (46, true, 0, 7, 4, &host, {});
        check (c.possible && c.value == 0, "no other plane, a current value above the primary: still the lowest (0)");
        c = chooseZpos (20, true, 0, 0, 0, &host, {});
        check (! c.possible, "a range that does not allow it (0 .. 0, our id below the primary's: needs 1): none");
        c = chooseZpos (46, true, 3, 7, 5, &host, { { 40, 1 } });
        check (! c.possible, "a range that starts above the pointer (3 .. 7, pointer at 1): none");
        c = chooseZpos (46, false, 0, 7, 2, &host, { { 40, 1 } });
        check (! c.possible, "an immutable property: none");
        c = chooseZpos (46, true, 0, 7, 2, nullptr, { { 40, 1 } });
        check (! c.possible, "no primary plane known: none");
        c = chooseZpos (46, true, 0, 7, 1, &host, { { 40, 3 } });
        check (c.possible && c.value == 0, "already in order at 1 (above the primary, below the pointer): the lowest, 0");
        c = chooseZpos (46, true, 0, 7, 0, &host, { { 40, 3 } });
        check (c.possible && c.value == 0, "already at the lowest: 0 (nothing to write)");
        c = chooseZpos (46, true, 0, 7, 2, &host, { { 40, 5 }, { 41, 2 } });
        check (c.possible && c.value == 0, "two other planes: below both (0)");
    }
    {
        ZposChoice c = chooseZpos (46, true, 2, 7, 6, &host, { { 40, 5 } });
        check (c.possible && c.value == 2, "a range 2 .. 7 with the pointer at 5: 2 (the lowest the range gives that is above the primary and below the pointer)");
    }
    // ---- the status of the pointer plane ----
    {
        PlaneInfo p40; p40.id = 40; p40.hasZpos = true; p40.zpos = 1;
        PlaneInfo p41; p41.id = 41;
        check (pointerStacking (46, true, 0, { p40 }) == PointerStacking::Above, "pointer above ours: above");
        check (pointerStacking (46, true, 2, { p40 }) == PointerStacking::Covered, "pointer under ours: covered");
        check (pointerStacking (46, true, 2, {}) == PointerStacking::None, "no other plane: none");
        check (pointerStacking (46, false, 0, { p41 }) == PointerStacking::Unknown, "no zpos: unknown");
        check (std::string (pointerStackingText (PointerStacking::Above)) == "above ours" && std::string (pointerStackingText (PointerStacking::Covered)) == "covered by ours" && std::string (pointerStackingText (PointerStacking::None)) == "none", "the status words");
    }
    // ---- the guard: the XL ----
    {
        FakeDriver d;
        d.add (34, 0);
        d.add (40, 1);
        d.add (46, 2);
        StackingGuard g (d);
        std::string st;
        g.describe (st);
        check (line (st, "display_plane_zpos") == "not read yet", "before the plane is taken: not read yet");
        g.take (46, 34, { 34, 40 });
        check (d.writes.size() == 1 && d.writes[0].first == 46 && d.writes[0].second == 0 && g.applied(), "the plane is taken: zpos 46 := 0 (one write)");
        st.clear();
        g.describe (st);
        check (line (st, "display_plane_zpos") == "rest 2, set 0", "status: display_plane_zpos: rest 2, set 0 (" + line (st, "display_plane_zpos") + ")");
        check (line (st, "display_pointer_plane") == "above ours", "status: the pointer plane is above ours (" + line (st, "display_pointer_plane") + ")");
        g.take (46, 34, { 34, 40 });
        check (d.writes.size() == 1, "taken again with the plane in order: no second write");
        d.planes[46].value = 2;     // a mode set of the application reset the plane
        g.take (46, 34, { 34, 40 });
        check (d.writes.size() == 2 && d.writes[1].second == 0 && g.rest() == 2, "the plane was reset by the application: set again, the rest value is still 2");
        g.release();
        check (d.writes.size() == 3 && d.writes[2].first == 46 && d.writes[2].second == 2 && ! g.applied(), "released: the zpos at rest (2) is written back");
        st.clear();
        g.describe (st);
        check (line (st, "display_plane_zpos").find ("rest 2, set 0") == 0 && line (st, "display_plane_zpos").find ("restored") != std::string::npos, "status after the release: " + line (st, "display_plane_zpos"));
        g.release();
        check (d.writes.size() == 3, "released twice: one write only");
        g.take (46, 34, { 34, 40 });
        check (d.writes.size() == 4 && d.planes[46].value == 0, "taken again after a release: set again");
        // the plane was taken by another program: the next spare plane; the old one is not touched
        d.add (52, 4);
        g.take (52, 34, { 34, 40 });
        check (d.planes[46].value == 0 && d.planes[52].value == 0 && g.rest() == 4, "another plane taken (the old one is lost): the old one is left alone, the new one is set, its rest value is 4");
        g.release();
        check (d.planes[52].value == 4 && d.planes[46].value == 0, "released: only the plane we hold is put back");
    }
    // ---- a failing write is not fatal ----
    {
        FakeDriver d;
        d.add (34, 0);
        d.add (40, 1);
        d.add (46, 2);
        d.failWrites = true;
        StackingGuard g (d);
        g.take (46, 34, { 34, 40 });
        std::string st;
        g.describe (st);
        check (! g.applied() && d.planes[46].value == 2 && line (st, "display_plane_zpos").find ("set failed: Permission denied") != std::string::npos, "the set fails: logged, the plane stays as it is (" + line (st, "display_plane_zpos") + ")");
        check (line (st, "display_pointer_plane") == "covered by ours", "... and the status says the pointer is covered by ours (" + line (st, "display_pointer_plane") + ")");
        g.release();
        check (d.writes.empty(), "nothing to put back");
        d.failWrites = false;
        g.take (46, 34, { 34, 40 });
        d.failWrites = true;
        g.release();
        st.clear();
        g.describe (st);
        check (g.failures() == 2 && line (st, "display_plane_zpos").find ("restore failed") != std::string::npos, "the restore fails: logged, not fatal (" + line (st, "display_plane_zpos") + ")");
    }
    // ---- the Key 61: no zpos property anywhere ----
    {
        FakeDriver d;
        d.addNone (28);
        d.addNone (32);
        d.addNone (36);
        StackingGuard g (d);
        g.take (36, 28, { 28, 32 });
        std::string st;
        g.describe (st);
        check (d.writes.empty() && line (st, "display_plane_zpos").find ("none (the plane has no zpos property)") == 0, "the Key 61: no write, status: " + line (st, "display_plane_zpos"));
        check (line (st, "display_pointer_plane").find ("unknown") == 0, "... pointer plane: " + line (st, "display_pointer_plane"));
        g.release();
        check (d.writes.empty(), "release: nothing written");
        FakeDriver d2;
        d2.addNone (28);
        d2.addNone (36);
        StackingGuard g2 (d2);
        g2.take (36, 28, { 28 });
        std::string st2;
        g2.describe (st2);
        check (line (st2, "display_pointer_plane") == "none", "the Key 61 with no other plane: pointer plane none (" + line (st2, "display_pointer_plane") + ")");
    }
    // ---- immutable property, plane with no primary, pointer hidden ----
    {
        FakeDriver d;
        d.add (34, 0, false);
        d.add (40, 1, false);
        d.add (46, 2, false);
        StackingGuard g (d);
        g.take (46, 34, { 34, 40 });
        std::string st;
        g.describe (st);
        check (d.writes.empty() && line (st, "display_plane_zpos").find ("immutable") != std::string::npos && line (st, "display_pointer_plane") == "covered by ours", "an immutable zpos: no write, status says so (" + line (st, "display_plane_zpos") + "; pointer " + line (st, "display_pointer_plane") + ")");
        FakeDriver e;
        e.add (34, 0);
        e.add (46, 2);
        StackingGuard h (e);
        h.take (46, 34, { 34 });      // the pointer is hidden (no plane): the application has only its primary
        std::string se;
        h.describe (se);
        check (e.planes[46].value == 0 && line (se, "display_pointer_plane") == "none", "the host's pointer is not on screen at the time: ours goes just above the primary, pointer plane: none");
        FakeDriver f;
        f.add (34, 0);
        f.add (40, 1);
        f.add (46, 2);
        StackingGuard k (f);
        k.take (46, 0, { 40 });       // the primary plane is not known
        std::string sk;
        k.describe (sk);
        check (f.writes.empty() && line (sk, "display_plane_zpos").find ("no value puts") != std::string::npos, "no primary plane known: nothing is written (" + line (sk, "display_plane_zpos") + ")");
    }
    std::printf (g_fails ? "stacking_host: FAILED (%d)\n" : "stacking_host: OK\n", g_fails);
    return g_fails ? 1 : 0;
}
