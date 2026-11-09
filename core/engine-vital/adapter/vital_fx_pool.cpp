// SynthyBoy - engine-vital adapter: the effect instance pool, builder and handover (see vital_fx_pool.h).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "vital_fx_pool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

#include "sound_engine.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "utils.h"
#include "value_switch.h"
#include "synthyboy/modulation.h"

namespace synthyboy
{
namespace
{
constexpr int kSettleChunks = 100;          // silent chunks rendered before a module is handed over (the same as engine reset ())
constexpr int kWorkQueueCapacity = 4096;    // dependency search queues of a router (a pointer each), as for the destinations of instance 0
} // namespace

FxPool::FxPool (sbvital::SoundEngine& soundEngine)
    : chain (soundEngine.effectChain())
{
    for (auto& r : ready)
        r.store (nullptr);
    for (auto& g : graveyard)
        g.store (nullptr);
    for (auto& g : latestGeneration)
        g.store (0);
    vitalBindingOf (0); // builds the index of the bindings now, not on the builder thread
    startThread();      // thread creation allocates: here (prepare), never on the audio thread
}

FxPool::~FxPool()
{
    if (thread.joinable())
    {
        stopping.store (true);
        {
            const std::lock_guard<std::mutex> lock (wakeMutex);
        }
        wake.notify_all();
        thread.join();
    }
    drain();
    for (auto& r : ready)
        if (FxBuilt* b = r.exchange (nullptr))
        {
            deleteModule (b->module);
            delete b;
        }
    // whatever is still alive was in the chain (the engine gives its packages back before the pool dies): the chain does not own it
    const std::lock_guard<std::mutex> lock (registryMutex);
    for (auto* module : live)
        delete module;
    live.clear();
}

void FxPool::deleteModule (sbvital::FxInstanceModule* module)
{
    if (module == nullptr)
        return;
    {
        const std::lock_guard<std::mutex> lock (registryMutex);
        live.erase (module);
    }
    delete module;
}

void FxPool::drain()
{
    // the chain's ring has one consumer: this function, serialised by the mutex (offline: the audio thread, realtime: the builder thread)
    while (true)
    {
        sbvital::FxInstanceModule* module = nullptr;
        {
            const std::lock_guard<std::mutex> lock (registryMutex);
            module = chain->popRetired();
        }
        if (module == nullptr)
            break;
        deleteModule (module);
    }
    while (true)
    {
        const unsigned r = graveRead.load (std::memory_order_relaxed);
        if (r == graveWrite.load (std::memory_order_acquire))
            break;
        FxBuilt* b = graveyard[r % kRing].load (std::memory_order_relaxed);
        graveRead.store (r + 1, std::memory_order_release);
        if (b != nullptr)
        {
            if (b->module != nullptr && ! b->handedOver)
                deleteModule (b->module); // never given to the chain: nobody else knows it
            delete b;
        }
    }
}

void FxPool::collectNow()
{
    {
        const std::lock_guard<std::mutex> lock (wakeMutex);
    }
    wake.notify_one(); // the builder thread deletes the retired modules (never the audio thread)
}

void FxPool::release (FxBuilt* built) noexcept
{
    if (built == nullptr)
        return;
    const unsigned w = graveWrite.load (std::memory_order_relaxed);
    if (w - graveRead.load (std::memory_order_acquire) >= kRing)
        return; // cannot happen: the builder thread empties the ring every few milliseconds; the package leaks rather than blocks
    graveyard[w % kRing].store (built, std::memory_order_relaxed);
    graveWrite.store (w + 1, std::memory_order_release);
}

FxBuilt* FxPool::takeReady (int slot, uint32_t currentGeneration) noexcept
{
    FxBuilt* b = ready[static_cast<size_t> (slot)].exchange (nullptr, std::memory_order_acq_rel);
    if (b == nullptr)
        return nullptr;
    if (b->generation != currentGeneration)
    {
        release (b); // a request that was overtaken
        return nullptr;
    }
    return b;
}

void FxPool::cancel (int slot) noexcept
{
    latestGeneration[static_cast<size_t> (slot)].store (++generations[static_cast<size_t> (slot)]);
}

bool FxPool::request (int slot, int type, const float* normalised, bool synchronous) noexcept
{
    Request r;
    r.slot = slot;
    r.type = type;
    r.beatsPerSecond = chain->liveBeatsPerSecond();   // the audio thread is the only one that may read the engine's outputs
    r.keytrack = chain->liveKeytrack();
    for (int k = 0; k < kFxInstanceParams; ++k)
        r.normalised[static_cast<size_t> (k)] = normalised[k];

    const unsigned w = requestWrite.load (std::memory_order_relaxed);
    if (w - requestRead.load (std::memory_order_acquire) >= kRing)
        return false; // try again in the next block
    r.generation = ++generations[static_cast<size_t> (slot)];
    latestGeneration[static_cast<size_t> (slot)].store (r.generation);
    requests[w % kRing] = r;
    inFlight.fetch_add (1, std::memory_order_acq_rel);
    requestWrite.store (w + 1, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock (wakeMutex);
    }
    wake.notify_one();
    if (synchronous)
    {
        // deterministic handover (offline renders, prepare): the audio thread waits for the builder, the take-over happens in this very block.
        // The audio thread itself allocates nothing; this is the pattern of the Convolve module (Quality::realtime == false)
        std::unique_lock<std::mutex> lock (doneMutex);
        done.wait (lock, [this] { return inFlight.load (std::memory_order_acquire) == 0; });
    }
    return true;
}

void FxPool::startThread()
{
    if (! thread.joinable())
        thread = std::thread ([this] { threadMain(); }); // allocates once, on the first realtime request (the engine starts it in prepare ())
}

void FxPool::threadMain()
{
    while (! stopping.load())
    {
        {
            std::unique_lock<std::mutex> lock (wakeMutex);
            wake.wait_for (lock, std::chrono::milliseconds (50), [this] { return stopping.load() || requestRead.load() != requestWrite.load(); });
        }
        drain();
        while (! stopping.load())
        {
            const unsigned r = requestRead.load (std::memory_order_relaxed);
            if (r == requestWrite.load (std::memory_order_acquire))
                break;
            const Request request = requests[r % kRing];
            requestRead.store (r + 1, std::memory_order_release);
            if (request.generation == latestGeneration[static_cast<size_t> (request.slot)].load())
            {
                FxBuilt* built = build (request);
                if (FxBuilt* old = ready[static_cast<size_t> (request.slot)].exchange (built, std::memory_order_acq_rel))
                {
                    deleteModule (old->module); // built but never taken: never handed to the chain
                    delete old;
                }
            }
            {
                const std::lock_guard<std::mutex> lock (doneMutex);
                inFlight.fetch_sub (1, std::memory_order_acq_rel);
            }
            done.notify_all();
        }
    }
}

FxBuilt* FxPool::build (const Request& r)
{
    const FxTypeDescriptor& d = fxTypeDescriptor (r.type);
    // the generators of the module's LFOs are seeded from a per thread counter: a fixed start per slot makes a module the same every time
    const int savedSeed = sbvital::utils::RandomGenerator::next_seed_;
    sbvital::utils::RandomGenerator::next_seed_ = 7000 + 100 * r.slot;

    auto* module = new sbvital::FxInstanceModule (d.module, chain->beatsPerSecondOutput(), chain->keytrackOutput(), chain->noteCounter());
    {
        const std::lock_guard<std::mutex> lock (registryMutex);
        live.insert (module);
    }
    module->init();
    module->prime (r.beatsPerSecond, r.keytrack);
    const int os = std::max (1, chain->getOversampleAmount());
    const int baseRate = chain->getSampleRate() / os;
    module->setSampleRate (baseRate);
    module->setOversampleAmount (os);
    module->updateAllModulationSwitches();
    sbvital::utils::RandomGenerator::next_seed_ = savedSeed;

    auto* built = new FxBuilt;
    built->slot = r.slot;
    built->type = r.type;
    built->generation = r.generation;
    built->module = module;
    built->count = d.count;

    const auto controls = module->getControls();
    std::set<sbvital::ProcessorRouter*> routers;
    for (int k = 0; k < d.count; ++k)
    {
        FxBuilt::Param& p = built->params[static_cast<size_t> (k)];
        p.source = &paramTable()[d.source[k]];
        p.binding = vitalBindingOf (static_cast<size_t> (d.source[k]));
        if (p.binding == nullptr)
            continue;
        const auto it = controls.find (p.binding->control);
        p.control = it != controls.end() ? it->second : nullptr;
        const ControlRange range = vitalControlRange (p.binding->control);
        p.lo = range.min;
        p.hi = range.max;
        p.mono = module->getMonoModulationDestination (p.binding->control);
        p.monoSwitch = module->getMonoModulationSwitch (p.binding->control);
        if (p.mono != nullptr && p.monoSwitch != nullptr && sbvital::Parameters::isParameter (p.binding->control))
        {
            // room for every modulation slot to be wired here at the same time, so that wiring allocates nothing (as for instance 0)
            p.mono->reserveInputs (kNumModSlots);
            p.scale = sbvital::Parameters::getParameterRange (p.binding->control);
            for (sbvital::ProcessorRouter* router = p.mono->router(); router != nullptr; router = router->router())
                routers.insert (router);
        }
        else
        {
            p.mono = nullptr;
            p.monoSwitch = nullptr;
        }
    }
    for (sbvital::ProcessorRouter* router : routers)
        router->reserveWorkspace (kWorkQueueCapacity);

    // the first values, then silence until the smoothers have settled (the same thing reset () does for instance 0)
    for (int k = 0; k < d.count; ++k)
    {
        built->appliedNorm[static_cast<size_t> (k)] = r.normalised[static_cast<size_t> (k)];
        FxBuilt::Param& p = built->params[static_cast<size_t> (k)];
        if (p.control == nullptr)
            continue;
        const double plain = fromNormalised (*p.source, r.normalised[static_cast<size_t> (k)]);
        const double value = convertParam (*p.binding, plain, static_cast<double> (baseRate));
        p.control->set (static_cast<float> (std::clamp (value, p.lo, p.hi)));
    }
    module->hardReset();
    std::vector<sbvital::poly_float> silence (static_cast<size_t> (sbvital::kMaxBufferSize) * static_cast<size_t> (sbvital::kMaxOversample), sbvital::poly_float (0.0f));
    for (int i = 0; i < kSettleChunks; ++i)
        module->run (silence.data(), sbvital::kMaxBufferSize * os);
    module->hardReset();
    return built;
}
} // namespace synthyboy
