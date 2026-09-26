#include "screen_registry.h"
#include "screen.h"

// Each screen file (sNN_name.cpp) owns a single statically-allocated
// instance (no heap, no PSRAM needed for the Screen object itself -- it is
// only a handful of pointers; the ~30 KB UiState snapshot is what goes in
// PSRAM, see screen_manager.cpp) and exposes it through one factory
// function declared here.
Screen *screenGetS01Painel();
Screen *screenGetS04FluxoEnergia();
Screen *screenGetS05Quarteto();
Screen *screenGetS06Eficiencia();
Screen *screenGetS07Sorte();
Screen *screenGetS10Halving();
Screen *screenGetS11Shares();
Screen *screenGetS12SeAchar();
Screen *screenGetS13VinteQuatroHoras();
Screen *screenGetS14Hashrate();
Screen *screenGetS15ContaDeLuz();
Screen *screenGetS16Sala();
Screen *screenGetS18Diario();
Screen *screenGetS19Zen();
Screen *screenGetS20Relogio();

// ---------------------------------------------------------------------
// HOW TO ADD A NEW SCREEN TO THE RODIZIO:
//   1. Implement the Screen interface (screen.h) in a new
//      screens/sNN_name.cpp, following s01_painel.cpp / s14_hashrate.cpp /
//      s19_zen.cpp as examples.
//   2. Expose it with a `Screen *screenGetSNNName();` factory function
//      (static instance, no heap allocation).
//   3. Add its `Screen*` declaration above and ONE line to the table
//      below.
//   4. Add the new .cpp to main/CMakeLists.txt SRCS.
//   5. Enable it in the rotation mask (uiRotationMask() in ui_data.cpp,
//      bit N = screen number N).
// Nothing else needs to change: ScreenManager reads this table through
// screenRegistryAll() and filters it against uiRotationMask() on its own.
// ---------------------------------------------------------------------
static const ScreenRegistryEntry kScreens[] = {
    {screenGetS01Painel()},
    {screenGetS04FluxoEnergia()},
    {screenGetS05Quarteto()},
    {screenGetS06Eficiencia()},
    {screenGetS07Sorte()},
    {screenGetS10Halving()},
    {screenGetS11Shares()},
    {screenGetS12SeAchar()},
    {screenGetS13VinteQuatroHoras()},
    {screenGetS14Hashrate()},
    {screenGetS15ContaDeLuz()},
    {screenGetS16Sala()},
    {screenGetS18Diario()},
    {screenGetS19Zen()},
    {screenGetS20Relogio()},
};

const ScreenRegistryEntry *screenRegistryAll(size_t *count)
{
    if (count) {
        *count = sizeof(kScreens) / sizeof(kScreens[0]);
    }
    return kScreens;
}
