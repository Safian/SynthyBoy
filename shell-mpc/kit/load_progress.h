// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdint>
#include <algorithm>
#include <array>

namespace kit {

struct ProgressSnapshot {
    bool active;
    uint32_t done;
    uint32_t total;
    uint32_t percent;
    uint32_t generation;
};

// Lock-free progress counter.
// Writer (helper thread): uses release/relaxed.
// Reader (UI thread): uses acquire/relaxed.
class LoadProgress {
public:
    void begin(uint32_t total) {
        done_.store(0, std::memory_order_relaxed);
        total_.store(total, std::memory_order_relaxed);
        active_.store(1, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_release);
    }

    void advance(uint32_t n) {
        done_.fetch_add(n, std::memory_order_relaxed);
    }

    void finish() {
        done_.store(total_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        active_.store(0, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_release);
    }

    void fail() {
        active_.store(0, std::memory_order_release);
        generation_.fetch_add(1, std::memory_order_release);
    }

    ProgressSnapshot snapshot() const {
        // Read active and generation with acquire to ensure visible state
        uint32_t gen = generation_.load(std::memory_order_acquire);
        bool active = active_.load(std::memory_order_acquire) != 0;
        uint32_t done = done_.load(std::memory_order_relaxed);
        uint32_t total = total_.load(std::memory_order_relaxed);
        
        uint32_t percent = 0;
        if (total > 0) {
            percent = std::min(100u, (done * 100) / total);
        }
        
        return { active, done, total, percent, gen };
    }

private:
    std::atomic<uint32_t> done_{0};
    std::atomic<uint32_t> total_{0};
    std::atomic<uint32_t> active_{0};
    std::atomic<uint32_t> generation_{0};
};

template<size_t N>
class LoadProgressSet {
public:
    LoadProgress& operator[](size_t i) { return progress_[i]; }
    
    ProgressSnapshot overall() const {
        uint32_t total_done = 0;
        uint32_t total_all = 0;
        bool any_active = false;
        
        for (const auto& p : progress_) {
            auto s = p.snapshot();
            if (s.active) {
                any_active = true;
                total_done += s.done;
                total_all += s.total;
            }
        }
        
        uint32_t percent = 0;
        if (total_all > 0) {
            percent = std::min(100u, (total_done * 100) / total_all);
        }
        
        return { any_active, total_done, total_all, percent, 0 };
    }

private:
    std::array<LoadProgress, N> progress_;
};

} // namespace kit
