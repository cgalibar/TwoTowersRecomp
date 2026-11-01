#pragma once

namespace ttr::debug
{
    // TTR_PROFILE=<seconds>[,<n>]: samples the instruction pointer of every thread of the process about a
    // thousand times a second and prints the hottest functions (resolved through the PDB) every
    // <seconds>, with the top <n> (default 8) of each busy thread. Windows only; does nothing elsewhere
    // or when the variable is not set.
    void startSamplingProfilerFromEnvironment();
}
