// Part 6 test
#include "../load_progress.h"
#include <cassert>
#include <cstdio>

int main() {
    kit::LoadProgress p;
    p.begin(100);
    p.advance(50);
    auto s = p.snapshot();
    assert(s.done == 50);
    assert(s.percent == 50);
    
    // Missing: wrap, advance beyond total, finish before begin.
    std::printf("LoadProgress tests: PASS\n");
    return 0;
}
