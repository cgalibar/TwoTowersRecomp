#include "debug/pad_script.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Test hooks of the runtime's libpad (ps2xRuntime/src/lib/Kernel/Stubs/Pad.cpp); buttons are active low.
namespace ps2_stubs
{
    void setPadOverrideState(uint16_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
    void clearPadOverrideState();
}

namespace ttr::debug
{
    namespace
    {
        struct PadStep
        {
            uint32_t from = 0;
            uint32_t to = 0;
            uint16_t pressed = 0; // active high, converted when applied
            uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
            std::string text;
        };

        uint16_t buttonBit(const std::string &name)
        {
            static const char *const kNames[16] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                                   "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
            for (uint16_t bit = 0; bit < 16u; ++bit)
            {
                if (name == kNames[bit])
                    return static_cast<uint16_t>(1u << bit);
            }
            return 0u;
        }

        bool parseInput(const std::string &input, PadStep &step)
        {
            const size_t equals = input.find('=');
            if (equals == std::string::npos)
            {
                const uint16_t bit = buttonBit(input);
                step.pressed |= bit;
                return bit != 0u;
            }
            const std::string axis = input.substr(0, equals);
            const int value = std::atoi(input.c_str() + equals + 1);
            if (value < 0 || value > 255)
                return false;
            uint8_t *target = axis == "lx" ? &step.lx : axis == "ly" ? &step.ly : axis == "rx" ? &step.rx
                                                            : axis == "ry" ? &step.ry
                                                                           : nullptr;
            if (!target)
                return false;
            *target = static_cast<uint8_t>(value);
            return true;
        }

        std::vector<PadStep> parseScript(const char *script)
        {
            std::vector<PadStep> steps;
            std::stringstream stream(script);
            std::string item;
            while (std::getline(stream, item, ';'))
            {
                const size_t colon = item.find(':');
                if (item.empty() || colon == std::string::npos)
                    continue;
                PadStep step;
                step.text = item;
                const std::string range = item.substr(0, colon);
                const size_t dash = range.find('-');
                step.from = static_cast<uint32_t>(std::strtoul(range.c_str(), nullptr, 10));
                step.to = dash == std::string::npos ? step.from + 5u
                                                    : static_cast<uint32_t>(std::strtoul(range.c_str() + dash + 1, nullptr, 10));
                std::stringstream inputs(item.substr(colon + 1));
                std::string input;
                bool valid = step.to >= step.from;
                while (valid && std::getline(inputs, input, '+'))
                    valid = parseInput(input, step);
                if (!valid)
                {
                    std::cout << "[ttr:pad] ignoring step '" << item << "'" << std::endl;
                    continue;
                }
                steps.push_back(step);
            }
            return steps;
        }

        const std::vector<PadStep> &script()
        {
            static const std::vector<PadStep> steps = []()
            {
                const char *env = std::getenv("TTR_PAD_SCRIPT");
                return env ? parseScript(env) : std::vector<PadStep>{};
            }();
            return steps;
        }
    }

    bool padScriptEnabled()
    {
        return !script().empty();
    }

    void applyPadScript(uint32_t vblank)
    {
        static const PadStep *active = nullptr;
        const PadStep *current = nullptr;
        for (const PadStep &step : script())
        {
            if (vblank >= step.from && vblank <= step.to)
            {
                current = &step;
                break;
            }
        }
        if (current == active)
            return;
        active = current;
        if (!current)
        {
            ps2_stubs::clearPadOverrideState();
            std::cout << "[ttr:pad] vblank=" << vblank << " released" << std::endl;
            return;
        }
        ps2_stubs::setPadOverrideState(static_cast<uint16_t>(~current->pressed), current->lx, current->ly,
                                       current->rx, current->ry);
        std::cout << "[ttr:pad] vblank=" << vblank << ' ' << current->text << std::endl;
    }
}
