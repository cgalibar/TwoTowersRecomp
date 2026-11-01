#pragma once

namespace ttr::debug
{
    // TTR_PROFILE=<seconds>: samples the instruction pointer of every thread of the process about a
    // thousand times a second and prints the hottest functions (resolved through the PDB) every
    // <seconds>. Windows only; does nothing elsewhere or when the variable is not set.
    void startSamplingProfilerFromEnvironment();
}
