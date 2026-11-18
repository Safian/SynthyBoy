// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ui_test.h"
#include "../load_bar.h"
#include "../../kit/load_progress.h"
#include <thread>
#include <vector>
#include <cstring>

using namespace synthyboy_mpc::ui;
using namespace uit;

void testLoadBar() {
    LoadBar b;
    b.place({10, 10, 200, 20});
    b.setEnabled(true);
    
    // Check identical draws
    b.state = { 37, 100, true, 1 };
    Scene s1(300, 100, kBackgroundRgba), s2(300, 100, kBackgroundRgba);
    b.draw(s1.builder());
    b.draw(s2.builder());
    check(s1.numOps() == s2.numOps() && std::memcmp(s1.data->ops, s2.data->ops, sizeof(gpu::SceneOp) * s1.numOps()) == 0, "load_bar: deterministic drawing");
    
    // Check unknown total
    b.state = { 0, 0, true, 1 };
    Scene s3(300, 100, kBackgroundRgba);
    b.draw(s3.builder());
    check(s3.numOps() > 0, "load_bar: draws something for unknown total");
}

void testLoadProgress() {
    kit::LoadProgress p;
    p.begin(100);
    
    // Writer thread
    std::thread writer([&]() {
        for(int i = 0; i < 5; ++i) {
            p.advance(20);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        p.finish();
    });
    
    // Reader thread
    uint32_t last_done = 0;
    for(int i = 0; i < 10; ++i) {
        auto snap = p.snapshot();
        check(snap.done >= last_done, "load_progress: monotone done");
        check(snap.percent <= 100, "load_progress: percent <= 100");
        last_done = snap.done;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    writer.join();
    auto snap = p.snapshot();
    check(!snap.active, "load_progress: inactive after finish");
}

int main() {
    testLoadBar();
    testLoadProgress();
    return g_fails > 0 ? 1 : 0;
}
