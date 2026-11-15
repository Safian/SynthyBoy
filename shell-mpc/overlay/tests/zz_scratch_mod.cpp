#include "../mod_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc; using namespace synthyboy_mpc::overlay;
int main(int, char** argv)
{
    demo::Rig<ModUi> r;
    r.refresh(2);
    auto& b = r.ui.matrix().board;
    std::printf("sources %d groups %d fits %d needed %d rect %d,%d,%d,%d\n", b.numSources(), b.numGroups(), b.fits(), b.heightFor(b.rect().w), b.rect().x, b.rect().y, b.rect().w, b.rect().h);
    int maxB = 0, maxR = 0;
    for (int i = 0; i < b.numSources(); ++i) { maxB = std::max(maxB, b.chipRect(i).bottom()); maxR = std::max(maxR, b.chipRect(i).right()); }
    std::printf("max bottom %d (rect bottom %d) max right %d (rect right %d)\n", maxB, b.rect().bottom(), maxR, b.rect().right());
    r.picture(argv[1], "mod_board.png");
    r.tapAt(b.chipRect(0).x + 20, b.chipRect(0).y + 20);
    r.picture(argv[1], "mod_board_list.png");
}
