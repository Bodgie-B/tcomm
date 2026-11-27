// The Christmas set's compile-time switches (xmas.h).
//
// SS_XMAS: the Christmas season in camp (Mike, 2026-11-26: on in the panel firmware). On by default: the
// panel compiles screensaver/core with no defines (tools/sync_ss_core.py), so it carries the season.
// Outside the season (Nov 26 to Jan 1, from Inputs::month and Inputs::mday) nothing of it shows and
// every plan and frame is what it would be with the switch off. -DSS_XMAS=0 (CMake -DSS_XMAS=OFF)
// builds the core without any of it, for the proof that nothing else moved.
//
// SS_XMAS_MOCKS: the stage 18 mockups Mike did not ask to build (the snow look, the north star; the
// sleigh is gone): off, so the panel carries none of their code. The stills tool can turn it on.
#pragma once

#ifndef SS_XMAS
#define SS_XMAS 1
#endif
#ifndef SS_XMAS_MOCKS
#define SS_XMAS_MOCKS 0
#endif

namespace ss {
// SS_XMAS as the core library was built with. The switch changes the World's layout (world.h), so a
// tool compiled by hand against a host-built library must use the same value: check this first.
int ssXmasSwitch();
}  // namespace ss
