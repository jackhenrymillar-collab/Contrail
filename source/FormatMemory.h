#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace contrail
{

// Each format keeps its own knob settings. Changing format stores the knobs of the
// format you leave and hands back the ones the next format had, so coming back to a
// format finds it the way you left it.
//
// Plain C++ with no locking: the processor guards it and does the JUCE side.
class FormatMemory
{
public:
    static constexpr std::size_t kNumKnobs = 7; // wow, flutter, drift, drive, age, tone, mix
    using Knobs = std::array<float, kNumKnobs>;

    explicit FormatMemory (std::size_t numFormats) : count (numFormats) {}

    std::size_t numFormats() const noexcept { return count; }
    int current() const noexcept { return active; }

    // Start tracking a format without storing or restoring anything (for example
    // after a preset or a session sets every knob at once).
    void setCurrent (int format) noexcept { active = format; }

    void store (int format, const Knobs& knobs) noexcept
    {
        if (valid (format))
            slots[static_cast<std::size_t> (format)] = knobs;
    }

    std::optional<Knobs> recall (int format) const noexcept
    {
        return valid (format) ? slots[static_cast<std::size_t> (format)] : std::nullopt;
    }

    // The selector moved to `format` while the knobs read `knobs`. Stores them for the
    // format being left and returns the knobs to apply, if `format` has any.
    std::optional<Knobs> switchTo (int format, const Knobs& knobs) noexcept
    {
        if (format == active)
            return std::nullopt;
        store (active, knobs);
        active = format;
        return recall (format);
    }

private:
    bool valid (int format) const noexcept
    {
        return format >= 0 && static_cast<std::size_t> (format) < count && static_cast<std::size_t> (format) < kMaxFormats;
    }

    static constexpr std::size_t kMaxFormats = 16;
    std::size_t count;
    int active = -1;
    std::array<std::optional<Knobs>, kMaxFormats> slots {};
};

} // namespace contrail
