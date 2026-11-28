// The plain-data half of the native choice lists (shell-mpc/native_choice_list.h): the slot table and its handle block, the per-slot debounce of the openers (one tap arrives as two press events), the
// session that opens on the page holding the current row, pages with PREVIOUS / NEXT, gives a row's value and closes on one tap, and closes on BACK. No plugin, no host.
// Usage: choice_list_host
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <cstdio>
#include <cstring>
#include <memory>

#include "native_choice_list.h"
#include "native_ui_params.h"

namespace cl = synthyboy_mpc::nativeui::choicelist;
namespace nu = synthyboy_mpc::nativeui;

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { std::printf ("FAIL %s\n", what); ++fails; } }

static void fill (cl::Items& items, int count)
{
    items.clear();
    items.setTitle ("TEST");
    char name[16];
    for (int i = 0; i < count; ++i)
    {
        std::snprintf (name, sizeof name, "Item %d", i);
        items.add (name, count > 1 ? static_cast<float> (i) / static_cast<float> (count - 1) : 0.0f);
    }
}

int main()
{
    // ---- the table and the handle block
    {
        check (cl::kSlots == 113, "113 slots");
        check (cl::slotOfHandle (21) == 0 && cl::slotOfHandle (3801) == cl::kSlots - 1 && cl::slotOfHandle (22) == -1, "slotOfHandle finds the first and the last slot, -1 for no opener");
        check (nu::kChoiceListOpen == 3903 - 2719 && nu::kChoiceList == 3904 - 2719
               && nu::kListState == nu::kChoiceList + cl::kSlots && nu::kCount == nu::kListState + nu::liststate::kHandles,
               "the choice-list and list-state handle blocks follow Play List");
        check (! nu::isTextGateOffset (nu::kChoiceListOpen) && nu::isChoiceListOffset (nu::kChoiceList) && nu::isChoiceListOffset (nu::kListState - 1)
               && ! nu::isChoiceListOffset (nu::kChoiceListOpen) && ! nu::isChoiceListOffset (nu::kListState), "the predicates of the choice-list block");
        check (nu::isListStateOffset (nu::kListState) && nu::isListStateOffset (nu::kCount - 1)
               && ! nu::isListStateOffset (nu::kListState - 1) && ! nu::isListStateOffset (nu::kCount)
               && nu::liststate::kHandles == 10, "the predicates of the list-state block");
        char rowBuf[32];
        nu::liststate::copyRowName (rowBuf, sizeof rowBuf, "Basic \xe2\x96\xb6");
        check (std::strcmp (rowBuf, "Basic") == 0, "copyRowName strips trailing folder/category arrow");
        nu::liststate::copyRowName (rowBuf, sizeof rowBuf, "Sine");
        check (std::strcmp (rowBuf, "Sine") == 0, "copyRowName keeps plain row names unchanged");
    }
    // ---- the port: one tap is one request, latest wins, per-slot debounce
    {
        cl::RequestPort port;
        check (port.take() == -1, "nothing pending at first");
        port.post (5, 1000.0);
        port.post (5, 1080.0);   // the second press event of the same tap
        check (port.take() == 5, "the first press is taken");
        check (port.take() == -1, "the second press inside the window is dropped");
        port.post (5, 1300.0);
        check (port.take() == 5, "a new tap after the window is a new request");
        port.post (7, 2000.0);
        port.post (9, 2010.0);
        check (port.take() == 9, "another slot replaces the pending one (latest wins)");
        port.post (9, 2100.0);
        check (port.take() == -1, "the debounce is per slot: the second press of slot 9 is dropped");
        port.post (7, 2100.0);
        check (port.take() == -1, "slot 7 was taken at 2000 (then replaced): its own window still drops it");
        port.post (7, 2300.0);
        check (port.take() == 7, "slot 7 after its own window is a new request");
        port.post (-1, 3000.0);
        port.post (cl::kSlots, 3000.0);
        check (port.take() == -1, "slots out of range are ignored");
        port.post (3, 4000.0);
        port.clear();
        check (port.take() == -1, "clear forgets what is pending");
        port.post (3, 4100.0);
        check (port.take() == -1, "clear keeps the debounce time: the late second press of a tap stays one tap");
    }
    // ---- items: the cap and the signature
    {
        auto items = std::make_unique<cl::Items>();
        fill (*items, cl::kMaxItems);
        check (items->count == cl::kMaxItems && ! items->add ("x", 0.0f) && items->count == cl::kMaxItems, "a full list drops a further row");
        const uint32_t a = items->signature();
        fill (*items, 10);
        const uint32_t b = items->signature();
        fill (*items, 10);
        check (items->signature() == b && a != b, "the same rows give the same signature, others another");
        items->values[3] = 0.5f;
        check (items->signature() != b, "a value change changes the signature");
        char longName[200];
        std::memset (longName, 'a', sizeof longName - 1);
        longName[sizeof longName - 1] = '\0';
        items->add (longName, 1.0f);
        check (std::strlen (items->names[items->count - 1]) == static_cast<size_t> (cl::kNameLen - 1), "a long name is cut to the row length");
    }
    // ---- the session: open on the page of the current row, paging, row -> value + close, BACK
    {
        auto session = std::make_unique<cl::Session>();
        fill (session->items, 0);
        check (! session->open (4, 0) && ! session->isOpen(), "an empty list does not open");
        fill (session->items, 5);    // single page: no prev, no next
        check (session->open (1, 0) && ! session->deeperLevel() && ! session->hasPrevious() && ! session->hasNext(), "single-page choice list: not deeper, no prev, no next");
        for (int r = 0; r < cl::kRows; ++r)
            check (! session->rowOpensLevel (r), "choice list rows are always final");
        fill (session->items, 17);   // pages of 7: 0..6, 7..13, 14..16
        check (session->open (4, 0) && ! session->hasPrevious() && session->hasNext(), "first page of 3: no prev, has next");
        check (session->open (4, 9) && session->isOpen() && session->slot() == 4, "opens for its slot");
        check (session->first() == 7 && session->rowsOnPage() == 7 && std::strcmp (session->rowName (2), "Item 9") == 0, "opens on the page that holds the current row");
        check (session->hasPrevious() && session->hasNext() && ! session->deeperLevel(), "middle page of 3: has prev, has next, not deeper");
        check (session->next() && session->first() == 14 && session->rowsOnPage() == 3, "NEXT: the last page holds the rest");
        check (session->hasPrevious() && ! session->hasNext(), "last page of 3: has prev, no next");
        check (! session->next() && session->first() == 14, "no page after the last one");
        check (std::strcmp (session->rowName (3), "") == 0, "a row past the end shows nothing");
        float value = -1.0f;
        check (! session->choose (3, value) && session->isOpen() && value == -1.0f, "a tap on an empty row does nothing");
        check (session->previous() && session->previous() && session->first() == 0 && ! session->previous(), "PREVIOUS down to the first page, not past it");
        check (! session->hasPrevious() && session->hasNext(), "back to first page: no prev, has next");
        check (session->choose (5, value) && value == 5.0f / 16.0f && ! session->isOpen() && session->slot() == -1, "one tap on a row gives its value and closes");
        check (! session->hasPrevious() && ! session->hasNext(), "closed list has no prev/next");
        check (! session->choose (0, value), "a closed list takes no row");
        check (session->open (2, 99) && session->first() == 0, "a current row out of range opens the first page");
        check (session->open (2, -1) && session->first() == 0, "no current row opens the first page");
        check (session->open (2, 16) && session->first() == 14, "the current last row opens the last page");
        session->close();   // BACK
        check (! session->isOpen() && session->rowsOnPage() == 0, "BACK closes");
        check (! session->open (cl::kSlots, 0), "a slot out of range does not open");
    }
    if (fails == 0)
        std::printf ("choice_list_host: all checks passed\n");
    return fails == 0 ? 0 : 1;
}
