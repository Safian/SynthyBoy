// SynthyBoy - window-relative native Mod controls.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace synthyboy_mpc::nativeui::mod
{
enum View { Hidden = 0, Matrix, MatrixScroll, DetailDelay, DetailBeats };
constexpr int kLastView = DetailBeats;
constexpr int kRows = 7;
constexpr int kLastSlot = 64;
enum RowField { Amount, Polarity, Bypass, kRowFields };
enum DetailField
{
    DetailAmount, DetailPolarity, DetailBypass,
    AuxScale, AuxInvert, AuxCurve, ShapeCurve, Stereo, Rise, Fall, Delay, DelaySync, kDetailFields
};
constexpr int kDetail = kRows * kRowFields;
constexpr int kFields = kDetail + kDetailFields;
constexpr int kOffsets = 1 + kRows + kFields;
enum Navigation { Mode, SourceGroup, DestinationGroup, Add, kHeaderNavigation };
enum RowNavigation { OpenSource, OpenDestination, OpenMore, kRowNavigation };
constexpr int kNavigation = kHeaderNavigation + kRows * kRowNavigation;
constexpr int kLastMode = 3;   // hidden, All, By Source, By Destination
constexpr int kLastSourceGroup = 5, kLastDestinationGroup = 10;
constexpr int navigationField (int row, RowNavigation action) noexcept
{
    return kHeaderNavigation + row * kRowNavigation + action;
}
constexpr const char* kNavigationNames[] {"Mode", "Source Group", "Destination Group", "Add"};
constexpr const char* kRowNavigationNames[] {"Source", "Target", "More"};
constexpr const char* kRowNames[] { "Amount", "Polarity", "Bypass" };
constexpr const char* kDetailNames[] {
    "Amount", "Polarity", "Bypass", "Aux Scale", "Aux Invert", "Aux Curve", "Shape Curve",
    "Stereo", "Rise", "Fall", "Delay", "Delay Sync"
};
}

namespace synthyboy_mpc::nativeui::moddetail
{
enum Navigation { Back, Delete, Source, Target, AuxSource, Confirm, kHandles };
constexpr const char* kNavigationNames[] { "Back", "Delete", "Source", "Target", "Aux Source", "Confirm" };
}

namespace synthyboy_mpc::nativeui::modboard
{
constexpr int kListRows = 7;
enum Field { Source, Group, Gate, ListRow, ListBack = ListRow + kListRows,
             ListPrev, ListNext, ListGroups, ListGate, ListTitle, ListRowGate,
             kHandles = ListRowGate + kListRows };
constexpr int kChoiceBias = 1024;
constexpr const char* fieldName (int field) noexcept
{
    if (field >= ListRow && field < ListBack) return "Choice";
    if (field >= ListRowGate) return "Visible";
    constexpr const char* names[] { "Source", "All Sources", "Gate", "Back", "Previous", "Next", "Groups", "List", "Select" };
    return field < ListRow ? names[field] : names[field - kListRows];
}
}
