// The Christmas set's compile-time switch (xmas.h). Off by default: the panel firmware compiles
// screensaver/core as it is (tools/sync_ss_core.py copies it with no defines), so there the Christmas
// code compiles to nothing, its hooks in world.cpp and critters.cpp vanish and the World holds no room
// for it. The host builds (screensaver/CMakeLists.txt, option SS_XMAS) turn it on for the tests, the
// stills and the soak.
#pragma once

#ifndef SS_XMAS
#define SS_XMAS 0
#endif

namespace ss {
// SS_XMAS as the core library was built with. The switch changes the World's layout (world.h), so a
// tool compiled by hand against a host-built library must use the same value: check this first.
int ssXmasSwitch();
}  // namespace ss
