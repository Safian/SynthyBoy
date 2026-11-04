// SynthyBoy - core: engine factory (selects between the own engine and, if built in, the Vital-derived one).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/engine.h"

#if SYNTHYBOY_HAS_VITAL
 #include "vital_engine.h" // core/engine-vital/adapter, only on the include path when SYNTHYBOY_WITH_VITAL=ON
#endif

namespace synthyboy
{
bool isEngineAvailable (EngineKind kind) noexcept
{
    switch (kind)
    {
        case EngineKind::own:   return true;
        case EngineKind::vital:
#if SYNTHYBOY_HAS_VITAL
            return true;
#else
            return false;
#endif
    }
    return false;
}

const char* engineName (EngineKind kind) noexcept
{
    switch (kind)
    {
        case EngineKind::own:   return "own";
        case EngineKind::vital: return "vital";
    }
    return "unknown";
}

std::unique_ptr<IEngine> createEngine (EngineKind kind, const char** errorMessage)
{
    if (errorMessage != nullptr)
        *errorMessage = nullptr;

    switch (kind)
    {
        case EngineKind::own:
            return createOwnEngine();

        case EngineKind::vital:
#if SYNTHYBOY_HAS_VITAL
            return createVitalEngine();
#else
            if (errorMessage != nullptr)
                *errorMessage = "this engine is not part of the build (configured with SYNTHYBOY_WITH_VITAL=OFF)";
            return nullptr;
#endif
    }

    if (errorMessage != nullptr)
        *errorMessage = "unknown engine kind";
    return nullptr;
}
} // namespace synthyboy
