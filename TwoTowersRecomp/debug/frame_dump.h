// Saves the frame the runtime last presented to a PNG file (bring-up debugging).
#pragma once

#include <filesystem>

class PS2Runtime;

namespace ttr::debug
{
    // Returns false when no frame has been presented yet or the file cannot be written.
    bool dumpPresentedFrame(PS2Runtime &runtime, const std::filesystem::path &path);
}
