// SynthyBoy - core: engine factory (the own engine).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "synthyboy/engine.h"

namespace synthyboy
{
bool isEngineAvailable (EngineKind kind) noexcept
{
    switch (kind)
    {
        case EngineKind::own: return true;
    }
    return false;
}

const char* engineName (EngineKind kind) noexcept
{
    switch (kind)
    {
        case EngineKind::own: return "own";
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
    }

    if (errorMessage != nullptr)
        *errorMessage = "unknown engine kind";
    return nullptr;
}
} // namespace synthyboy
