#include "../fx_ui.h"
#include "demo_common.h"
using namespace synthyboy_mpc; using namespace synthyboy_mpc::overlay;
int main(int, char** argv)
{
    demo::Rig<FxUi> r;
    r.refresh(2);
    auto& g = r.ui.screen().gallery;
    std::printf("types %d rect %d,%d,%d,%d\n", g.numTypes(), g.rect().x, g.rect().y, g.rect().w, g.rect().h);
    for (int i = 0; i < g.numTypes(); ++i) { auto c = g.cardRect(i); std::printf(" %s %d,%d,%d,%d\n", g.nameAt(i), c.x, c.y, c.w, c.h); }
    r.picture(argv[1], "fx_gallery.png");
    r.tapAt(g.cardRect(4).x + 20, g.cardRect(4).y + 20);
    std::printf("cards %d\n", r.ui.numCards());
    r.picture(argv[1], "fx_after.png");
}
