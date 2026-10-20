#include "FormatMemory.h"

#include <catch2/catch_test_macros.hpp>

using contrail::FormatMemory;

TEST_CASE ("Each format keeps its own knob settings")
{
    FormatMemory memory (6);
    memory.setCurrent (2);

    const FormatMemory::Knobs cassette { 0.15f, 0.10f, 50.0f, 5.0f, 30.0f, -10.0f, 100.0f };
    const FormatMemory::Knobs vhs { 0.30f, 0.12f, 50.0f, 2.0f, 55.0f, -30.0f, 100.0f };
    memory.store (4, vhs);

    // Leaving Cassette stores its knobs and hands back VHS's.
    auto restored = memory.switchTo (4, cassette);
    REQUIRE (restored.has_value());
    CHECK (*restored == vhs);
    CHECK (memory.current() == 4);

    // Edit a knob on VHS, then go back to Cassette: Cassette is as it was.
    auto edited = vhs;
    edited[0] = 0.9f;
    restored = memory.switchTo (2, edited);
    REQUIRE (restored.has_value());
    CHECK (*restored == cassette);

    // And VHS remembers the edit.
    restored = memory.switchTo (4, cassette);
    REQUIRE (restored.has_value());
    CHECK ((*restored)[0] == 0.9f);
}

TEST_CASE ("A format with nothing stored keeps the current knobs")
{
    FormatMemory memory (6);
    memory.setCurrent (0);
    const FormatMemory::Knobs knobs { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    CHECK_FALSE (memory.switchTo (4, knobs).has_value());
    CHECK (memory.recall (0) == knobs);

    // Re-selecting the same format changes nothing.
    CHECK_FALSE (memory.switchTo (4, knobs).has_value());
    CHECK_FALSE (memory.recall (4).has_value());

    // Out-of-range formats are ignored.
    memory.store (40, knobs);
    CHECK_FALSE (memory.recall (40).has_value());
}
