// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ParameterMask.h"

#include <iostream>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

// Wide enough to have a word boundary in it, whatever the registry's cap
// happens to be today. The whole point of this suite is to prove the arithmetic
// at id 64 BEFORE any parameter is allowed to live there.
using Wide = BitMaskValue<128>;
using AtomicWide = AtomicBitMask<128>;
} // namespace

int main()
{
    std::cout << "Parameter mask" << std::endl << std::endl;

    // ---- Word arithmetic ----------------------------------------------------
    {
        check(BitMaskValue<64>::words == 1, "64 bits is one word");
        check(BitMaskValue<65>::words == 2, "65 bits is two words");
        check(BitMaskValue<128>::words == 2, "128 bits is two words");
        check(BitMaskValue<129>::words == 3, "129 bits is three words");

        std::cout << "  word count rounds up, never down" << std::endl;
    }

    // ---- Empty --------------------------------------------------------------
    {
        Wide mask;
        check(!mask.any(), "a fresh mask holds nothing");

        bool anySet = false;
        for (int id = 0; id < 128; ++id)
            if (mask.test(id))
                anySet = true;

        check(!anySet, "and no single id reads as set");
        std::cout << "  a fresh mask is empty" << std::endl;
    }

    // ---- THE ONE THAT MATTERS ----------------------------------------------
    //
    // On x86, 1ull << 64 is 1 rather than 0: the shift count wraps. Written
    // inline, as the code this replaces wrote it, setting parameter 64 would
    // set parameter 0 as well - and parameter 0 is the Wave dial. Nothing warns,
    // nothing crashes, and the symptom appears somewhere else entirely.
    //
    // This is the reason the type exists.
    {
        Wide mask = Wide::forParameter(64);

        check(mask.test(64), "id 64 is set");
        check(!mask.test(0), "and id 0 is NOT - the shift did not wrap");
        check(mask.any(), "the mask reports holding something");

        Wide zero = Wide::forParameter(0);
        check(zero.test(0), "id 0 is set");
        check(!zero.test(64), "and id 64 is not - the pair do not share a bit");

        std::cout << "  id 64 and id 0 are genuinely different bits" << std::endl;
    }

    // ---- Either side of the boundary ---------------------------------------
    {
        for (int id : {62, 63, 64, 65, 126, 127})
        {
            Wide mask = Wide::forParameter(id);

            for (int other = 0; other < 128; ++other)
            {
                const bool expected = (other == id);
                if (mask.test(other) != expected)
                {
                    check(false, "id " + juce::String(id) + " leaked into id "
                                     + juce::String(other));
                    break;
                }
            }
        }

        std::cout << "  62, 63, 64, 65, 126 and 127 each set exactly themselves" << std::endl;
    }

    // ---- Out of range -------------------------------------------------------
    // Matches what the StepSequencer guards already do: silently ignore rather
    // than write somewhere unrelated.
    {
        AtomicWide mask;
        mask.set(-1, true);
        mask.set(128, true);
        mask.set(9999, true);

        check(!mask.load().any(), "ids outside the range set nothing at all");

        Wide value;
        check(!value.test(-1), "a negative id reads as unset");
        check(!value.test(128), "an id past the end reads as unset");

        std::cout << "  out-of-range ids are ignored, not folded back in" << std::endl;
    }

    // ---- Set and clear ------------------------------------------------------
    {
        AtomicWide mask;
        mask.set(63, true);
        mask.set(64, true);
        mask.set(65, true);

        auto value = mask.load();
        check(value.test(63) && value.test(64) && value.test(65),
              "three ids across the boundary are all set");

        mask.set(64, false);
        value = mask.load();

        check(value.test(63), "clearing 64 leaves 63 alone");
        check(!value.test(64), "64 is clear");
        check(value.test(65), "and leaves 65 alone");

        std::cout << "  clearing one id across the boundary leaves its neighbours" << std::endl;
    }

    // ---- Union --------------------------------------------------------------
    {
        const auto low = Wide::forParameter(5);
        const auto high = Wide::forParameter(100);
        const auto both = low | high;

        check(both.test(5) && both.test(100), "a union spanning both words holds both");
        check(!both.test(4) && !both.test(101), "and nothing either side of them");

        std::cout << "  union works across the word boundary" << std::endl;
    }

    // ---- merge and andNot ---------------------------------------------------
    // The two operations releaseFromRecording and the recording latch need.
    {
        AtomicWide mask;
        mask.merge(Wide::forParameter(3) | Wide::forParameter(70));

        auto value = mask.load();
        check(value.test(3) && value.test(70), "merge sets both words");

        mask.andNot(Wide::forParameter(70));
        value = mask.load();

        check(value.test(3), "andNot leaves the id it was not given");
        check(!value.test(70), "and removes the one it was");

        mask.clearAll();
        check(!mask.load().any(), "clearAll empties every word");

        std::cout << "  merge, andNot and clearAll span both words" << std::endl;
    }

    // ---- exchangeZero -------------------------------------------------------
    {
        AtomicWide mask;
        mask.set(1, true);
        mask.set(127, true);

        const auto taken = mask.exchangeZero();

        check(taken.test(1) && taken.test(127), "the drain returns what was held");
        check(!mask.load().any(), "and leaves the mask empty");

        std::cout << "  exchangeZero returns everything and keeps nothing" << std::endl;
    }

    // ---- all ----------------------------------------------------------------
    // What clearAllLanes passes to releaseFromRecording, in place of ~0ull.
    {
        const auto everything = Wide::all();

        bool allSet = true;
        for (int id = 0; id < 128; ++id)
            if (!everything.test(id))
                allSet = false;

        check(allSet, "all() holds every id in range");
        check(everything.any(), "and reports holding something");

        // A capacity that is not a whole number of words must not claim the
        // bits above it. 100 ids is one full word plus 36.
        const auto partial = BitMaskValue<100>::all();
        check(partial.w[0] == ~uint64_t{0}, "a full word is full");
        check(partial.w[1] == (~uint64_t{0} >> 28),
              "and the partial word stops at the capacity");
        check(partial.test(99) && !partial.test(100),
              "the last id in range is held, the first past it is not");

        // The operation clearAllLanes actually performs.
        AtomicWide mask;
        mask.set(0, true);
        mask.set(64, true);
        mask.set(127, true);
        mask.andNot(Wide::all());

        check(!mask.load().any(), "andNot(all()) empties the mask");

        std::cout << "  all() covers the range and stops at it" << std::endl;
    }

    // ---- The registry's own alias -------------------------------------------
    // Not testing the cap's value, which is free to move. Testing that the
    // alias is wide enough for every id the registry can hand out, which is the
    // invariant that must hold whatever the cap becomes.
    {
        check(ParameterMask::capacity >= paramreg::count(),
              "the mask is wide enough for every registered parameter");

        ParameterMask mask = ParameterMask::forParameter(paramreg::count() - 1);
        check(mask.test(paramreg::count() - 1), "the highest registered id fits");

        std::cout << "  registry has " << paramreg::count() << " parameters, mask holds "
                  << ParameterMask::capacity << " in " << ParameterMask::words << " word(s)"
                  << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL PARAMETER MASK TESTS PASSED" << std::endl;
    else
        std::cout << failures << " PARAMETER MASK TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
