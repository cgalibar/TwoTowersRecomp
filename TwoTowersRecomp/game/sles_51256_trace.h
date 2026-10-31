// Opt-in bring-up tracing for SLES-51256 (enabled with the TTR_TRACE environment variable).
#pragma once

class PS2Runtime;

namespace ttr::sles51256
{
    void installTracing(PS2Runtime &runtime);
}
