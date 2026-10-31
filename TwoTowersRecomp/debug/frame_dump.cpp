#include "debug/frame_dump.h"

#include "ps2_runtime.h"
#include "runtime/gs/gs_frontend.h"

#include "raylib.h"

#include <cstdint>
#include <vector>

namespace ttr::debug
{
    bool dumpPresentedFrame(PS2Runtime &runtime, const std::filesystem::path &path)
    {
        std::vector<uint8_t> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
        if (!runtime.gs().copyLatchedHostPresentationFrame(pixels, width, height) || width == 0 || height == 0 ||
            pixels.size() < static_cast<size_t>(width) * height * 4u)
        {
            return false;
        }

        // GS alpha is 0..0x80 and often 0 in the front buffer; force opaque for viewing.
        for (size_t i = 3; i < pixels.size(); i += 4)
        {
            pixels[i] = 0xFF;
        }

        Image image{};
        image.data = pixels.data();
        image.width = static_cast<int>(width);
        image.height = static_cast<int>(height);
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        std::filesystem::create_directories(path.parent_path());
        return ExportImage(image, path.string().c_str());
    }
}
