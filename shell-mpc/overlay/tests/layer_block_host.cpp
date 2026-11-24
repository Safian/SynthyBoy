// SynthyBoy - host unit and ThreadSanitizer stress tests for gpu/layer_block.h.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../gpu/layer_block.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace gpu = synthyboy_mpc::overlay::gpu;

static int g_fails = 0;

static void check (bool ok, const char* msg)
{
    if (! ok)
    {
        std::fprintf (stderr, "FAIL: %s\n", msg);
        ++g_fails;
    }
}

static LayerFields makeFields (uint32_t id, uint32_t enabled = 1)
{
    LayerFields f {};
    f.helper_pid = 1000 + static_cast<int32_t> (id % 17);
    f.plane_id = 46;
    f.crtc_id = 82;
    f.fb_id = id;
    f.crtc_x = static_cast<int32_t> (id * 3u);
    f.crtc_y = static_cast<int32_t> (id * 7u);
    f.crtc_w = 100u + (id & 0xffu);
    f.crtc_h = 200u + ((id >> 8) & 0xffu);
    f.src_x = (id & 0x3fu) << 16;
    f.src_y = ((id >> 6) & 0x3fu) << 16;
    f.src_w = f.crtc_w << 16;
    f.src_h = f.crtc_h << 16;
    f.enabled = enabled;
    f.page_id = static_cast<int32_t> (id % 22);
    f.expected_marker_l = 0x103a2c + id;
    f.expected_marker_r = 0x3a1064 + id;
    f.rotation = 270;
    return f;
}

static bool fieldsConsistent (const LayerFields& f)
{
    if (f.fb_id == 0) return false;
    if (f.plane_id != 46 || f.crtc_id != 82) return false;
    if (f.helper_pid != 1000 + static_cast<int32_t> (f.fb_id % 17)) return false;
    if (f.crtc_x != static_cast<int32_t> (f.fb_id * 3u)) return false;
    if (f.crtc_y != static_cast<int32_t> (f.fb_id * 7u)) return false;
    if (f.crtc_w != 100u + (f.fb_id & 0xffu)) return false;
    if (f.crtc_h != 200u + ((f.fb_id >> 8) & 0xffu)) return false;
    if (f.src_x != ((f.fb_id & 0x3fu) << 16)) return false;
    if (f.src_y != (((f.fb_id >> 6) & 0x3fu) << 16)) return false;
    if (f.src_w != (f.crtc_w << 16) || f.src_h != (f.crtc_h << 16)) return false;
    if (f.page_id != static_cast<int32_t> (f.fb_id % 22)) return false;
    if (f.expected_marker_l != 0x103a2c + f.fb_id) return false;
    if (f.expected_marker_r != 0x3a1064 + f.fb_id) return false;
    if (f.rotation != 270) return false;
    return true;
}

static void testBasicProtocol()
{
    LayerBlock b;
    layer_block_init (&b);
    check (layer_block_valid (&b), "init sets valid magic and version");
    check (layer_block_presented_seq (&b) == 0ULL, "init sets presented_seq to 0");

    LayerFields out {};
    check (! gpu::try_claim (&b, out), "empty block cannot be claimed");
    check (! gpu::hook_alive (&b, 1000000ULL), "inactive hook is not alive");

    layer_block_set_heartbeat (&b, 1000000000ULL, 1u);
    check (gpu::hook_alive (&b, 1100000000ULL), "hook alive within 250 ms");
    check (gpu::hook_alive (&b, 1250000000ULL), "hook alive at exact 250 ms boundary");
    check (! gpu::hook_alive (&b, 1250000001ULL), "hook stale after 250 ms");
    check (gpu::hook_alive (&b, 999999999ULL), "clock skew slightly behind heartbeat stays alive");

    layer_block_set_active (&b, 0u);
    check (! gpu::hook_alive (&b, 1000000100ULL), "hook_active=0 disables alive immediately");

    LayerFields f = makeFields (42, 1);
    const uint64_t seq = gpu::publish (&b, f);
    check (seq == 2ULL, "first publish seq is 2");

    // Test seqlock read (lock-free)
    LayerFields rsnap {};
    check (gpu::read (&b, rsnap), "read succeeds on published seq");
    check (rsnap.seq == 2ULL && rsnap.fb_id == 42u && fieldsConsistent (rsnap), "read fields match published");
    check (rsnap.page_id == static_cast<int32_t> (42 % 22), "read page_id matches");
    check (rsnap.expected_marker_l == 0x103a2c + 42, "read expected_marker_l matches");
    check (rsnap.expected_marker_r == 0x3a1064 + 42, "read expected_marker_r matches");
    check (rsnap.rotation == 270, "read rotation matches");

    // Guard plane off flag test
    check (gpu::guard_plane_off (&b) == 0u, "guard_plane_off starts 0");
    gpu::set_guard_plane_off (&b, 1u);
    check (gpu::guard_plane_off (&b) == 1u, "guard_plane_off set to 1");
    gpu::set_guard_plane_off (&b, 0u);
    check (gpu::guard_plane_off (&b) == 0u, "guard_plane_off cleared to 0");

    check (gpu::try_claim (&b, out), "claim succeeds on newly published seq");
    check (out.seq == 2ULL && out.fb_id == 42u && fieldsConsistent (out), "claimed fields match published");
    check (! gpu::try_claim (&b, out), "same seq cannot be claimed twice");

    // presented_seq tracking
    check (gpu::presented_seq (&b) == 0ULL, "presented_seq still 0 before presentation");
    gpu::set_presented_seq (&b, seq);
    check (gpu::presented_seq (&b) == seq, "presented_seq advances to published seq");

    // Version mismatch: block with version 2 must not be valid in version 3
    b.version.store (2u, std::memory_order_release);
    check (! layer_block_valid (&b), "version 2 block is NOT valid in version 3");
    check (gpu::presented_seq (&b) == 0ULL, "presented_seq returns 0 on invalid block");
    check (! gpu::try_claim (&b, out), "try_claim fails on invalid block");
    check (! gpu::read (&b, rsnap), "read fails on invalid block");
    b.version.store (LAYER_BLOCK_VERSION, std::memory_order_release);
    check (layer_block_valid (&b), "version restored to LAYER_BLOCK_VERSION is valid");

    // Odd seq (writer active) must fail try_claim
    b.seq.store (3ULL, std::memory_order_release);
    check (! gpu::try_claim (&b, out), "odd seq returns false");
    b.seq.store (2ULL, std::memory_order_release);
}

static void testFileBackedMmap()
{
    char tmpl[] = "/tmp/sb-layer-block-test-XXXXXX";
    const int fd = ::mkstemp (tmpl);
    check (fd >= 0, "mkstemp succeeds");
    if (fd < 0) return;
    ::close (fd);
    ::unlink (tmpl);

    ::setenv (LAYER_BLOCK_PATH_ENV, tmpl, 1);
    check (std::strcmp (layer_block_path (), tmpl) == 0, "env var overrides layer block path");

    LayerBlock* writer = layer_block_map (layer_block_path (), true);
    check (writer != nullptr, "creator maps layer block");
    LayerBlock* reader = layer_block_map (layer_block_path (), false);
    check (reader != nullptr, "reader maps existing layer block");

    if (writer && reader)
    {
        const uint64_t s = gpu::publish (writer, makeFields (77, 0));
        check (s == 2ULL, "file-backed publish returns seq 2");
        LayerFields out {};
        check (gpu::try_claim (reader, out), "reader claims across mmap");
        check (out.fb_id == 77u && out.enabled == 0u && fieldsConsistent (out), "reader snapshot matches");
        check (! gpu::try_claim (writer, out), "writer sees seq already claimed");
    }
    layer_block_unmap (reader);
    layer_block_unmap (writer);
    ::unlink (tmpl);
    ::unsetenv (LAYER_BLOCK_PATH_ENV);
}

static void testThreadedStress()
{
    LayerBlock b;
    layer_block_init (&b);

    constexpr int kWriters = 2;
    constexpr int kReaders = 2;
    constexpr int kItersPerWriter = 500000; // 1e6 total publishes across two writers

    std::atomic<bool> start { false };
    std::atomic<int> writersDone { 0 };
    std::atomic<uint64_t> tornCount { 0 };
    std::vector<uint64_t> claimedByReader[kReaders];
    for (int r = 0; r < kReaders; ++r)
        claimedByReader[r].reserve (100000);

    std::thread writers[kWriters];
    for (int w = 0; w < kWriters; ++w)
    {
        writers[w] = std::thread ([&, w] ()
        {
            while (! start.load (std::memory_order_acquire)) {}
            for (int i = 1; i <= kItersPerWriter; ++i)
            {
                const uint32_t id = static_cast<uint32_t> (w * kItersPerWriter + i);
                gpu::publish (&b, makeFields (id, id & 1u));
            }
            writersDone.fetch_add (1, std::memory_order_release);
        });
    }

    std::thread readers[kReaders];
    for (int r = 0; r < kReaders; ++r)
    {
        readers[r] = std::thread ([&, r] ()
        {
            while (! start.load (std::memory_order_acquire)) {}
            LayerFields out {};
            for (;;)
            {
                if (gpu::try_claim (&b, out))
                {
                    if ((out.seq & 1ULL) != 0ULL || ! fieldsConsistent (out))
                        tornCount.fetch_add (1, std::memory_order_relaxed);
                    claimedByReader[r].push_back (out.seq);
                }
                else if (writersDone.load (std::memory_order_acquire) == kWriters)
                {
                    // One last check after writers finished
                    if (gpu::try_claim (&b, out))
                    {
                        if ((out.seq & 1ULL) != 0ULL || ! fieldsConsistent (out))
                            tornCount.fetch_add (1, std::memory_order_relaxed);
                        claimedByReader[r].push_back (out.seq);
                    }
                    break;
                }
            }
        });
    }

    start.store (true, std::memory_order_release);
    for (auto& t : writers) t.join ();
    for (auto& t : readers) t.join ();

    check (tornCount.load () == 0ULL, "no torn snapshot across 1e6 publishes");
    const uint64_t finalSeq = b.seq.load (std::memory_order_acquire);
    check (finalSeq == static_cast<uint64_t> (kWriters * kItersPerWriter) * 2ULL, "final seq equals 2 * 1e6");

    std::vector<uint64_t> all;
    for (int r = 0; r < kReaders; ++r)
        all.insert (all.end (), claimedByReader[r].begin (), claimedByReader[r].end ());
    std::sort (all.begin (), all.end ());

    bool duplicate = false;
    for (size_t i = 1; i < all.size (); ++i)
    {
        if (all[i] == all[i - 1])
        {
            duplicate = true;
            break;
        }
    }
    check (! duplicate, "a claimed seq is never claimed twice");
    check (! all.empty (), "readers claimed snapshots under contention");
    std::printf ("layer_block_host: %llu publishes, %zu claims, 0 torn, 0 duplicates\n",
                 static_cast<unsigned long long> (finalSeq / 2ULL), all.size ());
}

static LayerFields makeUniformPatternFields (uint32_t counter)
{
    LayerFields f {};
    f.helper_pid        = static_cast<int32_t> (counter);
    f.plane_id          = counter;
    f.crtc_id           = counter;
    f.fb_id             = counter;
    f.crtc_x            = static_cast<int32_t> (counter);
    f.crtc_y            = static_cast<int32_t> (counter);
    f.crtc_w            = counter;
    f.crtc_h            = counter;
    f.src_x             = counter;
    f.src_y             = counter;
    f.src_w             = counter;
    f.src_h             = counter;
    f.enabled           = counter;
    f.page_id           = static_cast<int32_t> (counter);
    f.expected_marker_l = counter;
    f.expected_marker_r = counter;
    f.rotation          = counter;
    return f;
}

static bool uniformPatternConsistent (const LayerFields& f)
{
    const uint32_t c = f.fb_id;
    return static_cast<uint32_t> (f.helper_pid) == c &&
           f.plane_id == c &&
           f.crtc_id  == c &&
           static_cast<uint32_t> (f.crtc_x) == c &&
           static_cast<uint32_t> (f.crtc_y) == c &&
           f.crtc_w   == c &&
           f.crtc_h   == c &&
           f.src_x    == c &&
           f.src_y    == c &&
           f.src_w    == c &&
           f.src_h    == c &&
           f.enabled  == c &&
           static_cast<uint32_t> (f.page_id) == c &&
           f.expected_marker_l == c &&
           f.expected_marker_r == c &&
           f.rotation == c;
}

static void testUniformPatternStress ()
{
    LayerBlock b;
    layer_block_init (&b);

    constexpr int kIters = 1000000; // 1e6 iterations written by the writer thread
    constexpr int kReaders = 2;

    std::atomic<bool> start { false };
    std::atomic<bool> writerDone { false };
    std::atomic<uint64_t> tornCount { 0 };
    std::atomic<uint64_t> totalClaims { 0 };

    std::thread writer ([&] ()
    {
        while (! start.load (std::memory_order_acquire)) {}
        for (uint32_t i = 1; i <= kIters; ++i)
        {
            gpu::publish (&b, makeUniformPatternFields (i));
        }
        writerDone.store (true, std::memory_order_release);
    });

    std::thread readers[kReaders];
    for (int r = 0; r < kReaders; ++r)
    {
        readers[r] = std::thread ([&] ()
        {
            while (! start.load (std::memory_order_acquire)) {}
            LayerFields out {};
            for (;;)
            {
                if (gpu::try_claim (&b, out))
                {
                    totalClaims.fetch_add (1, std::memory_order_relaxed);
                    if ((out.seq & 1ULL) != 0ULL || ! uniformPatternConsistent (out))
                        tornCount.fetch_add (1, std::memory_order_relaxed);
                }
                else if (writerDone.load (std::memory_order_acquire))
                {
                    if (gpu::try_claim (&b, out))
                    {
                        totalClaims.fetch_add (1, std::memory_order_relaxed);
                        if ((out.seq & 1ULL) != 0ULL || ! uniformPatternConsistent (out))
                            tornCount.fetch_add (1, std::memory_order_relaxed);
                    }
                    break;
                }
            }
        });
    }

    start.store (true, std::memory_order_release);
    writer.join ();
    for (auto& t : readers) t.join ();

    check (tornCount.load () == 0ULL, "no torn snapshot across 1e6 recognisable pattern iterations");
    const uint64_t finalSeq = b.seq.load (std::memory_order_acquire);
    check (finalSeq == static_cast<uint64_t> (kIters) * 2ULL, "final seq equals 2 * 1e6");
    check (totalClaims.load () > 0ULL, "readers claimed snapshots during pattern stress");
    std::printf ("uniform_pattern_stress: %d publishes, %llu claims, 0 torn (all fields = same counter)\n",
                 kIters, static_cast<unsigned long long> (totalClaims.load ()));
}

int main ()
{
    testBasicProtocol ();
    testFileBackedMmap ();
    testThreadedStress ();
    testUniformPatternStress ();
    if (g_fails != 0)
    {
        std::fprintf (stderr, "layer_block_host FAILED (%d failures)\n", g_fails);
        return 1;
    }
    std::printf ("layer_block_host PASSED\n");
    return 0;
}
