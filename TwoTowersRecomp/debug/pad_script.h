#pragma once

#include <cstdint>

namespace ttr::debug
{
    // TTR_PAD_SCRIPT=<step>;<step>... drives the pad from the VBlank count, for reproducible runs. A step is
    // <from>[-<to>]:<input>[+<input>...] and holds its inputs from VBlank <from> to <to> (from + 5 when
    // omitted). An input is a button (start, select, cross, circle, square, triangle, l1, r1, l2, r2, l3, r3,
    // up, down, left, right) or a stick axis (lx=, ly=, rx=, ry= with 0-255, 128 = centre). Outside the
    // steps the keyboard and the gamepad work as usual.
    bool padScriptEnabled();

    // Called once per VBlank.
    void applyPadScript(uint32_t vblank);
}
