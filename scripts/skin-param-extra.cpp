// Prints how many plugin parameters the MPC build registers after the engine parameters of docs/params.json
// (synthyboy_mpc.cpp, numParams: live LFO + spectrum, pad pressure, overlay marker, play, Q, native UI).
// Used by scripts/validate_synthyboy_skin.py; compiled with: c++ -std=c++17 -Ishell-mpc -Icore/include
#include <cstdio>
#include "native_ui_params.h"
#include "play_params.h"
#include "q_router.h"
#include "spectrum_bands.h"
#include "lfo_live.h"
#include "pad_pressure.h"
namespace synthyboy { const ParamInfo* findParamByKey (const char*) noexcept { return nullptr; } int findParamIndex (uint32_t) noexcept { return -1; } }
int main()
{
    using namespace synthyboy_mpc;
    std::printf ("%d\n", kLfoLiveCount + kSpectrumBands + kPadPressureParams + 1 + play::kParams + QRouter::kCount + nativeui::kCount);
}
