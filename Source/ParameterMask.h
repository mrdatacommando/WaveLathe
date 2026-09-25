// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ParameterRegistry.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace wavelathe
{
// A set of parameter ids, held as bits, readable from the audio thread without
// a lock.
//
// This exists to replace a bare std::atomic<uint64_t> and seven expressions of
// the form:
//
//     uint64_t bit = 1ull << (uint64_t) parameterId;
//
// which are correct only while there are 64 parameters or fewer. On x86,
// 1ull << 64 is 1 - the shift count wraps rather than saturating - so the first
// parameter past the word boundary would silently share a bit with parameter 0,
// which is the Wave dial. It is undefined behaviour, it warns about nothing, it
// does not crash, and it would present as an inexplicable fault in the
// oscillator section.
//
// Everything below indexes by (id / 64, id % 64). The second is always less
// than 64 by construction, so the shift that caused the problem cannot be
// written here at all. That is the whole reason this is a type rather than a
// wider integer.
//
// Parameterised on capacity so the boundary can be tested before the cap is
// raised: ParameterMask is one word today and the case that matters lives at
// ids 64 and 65, which do not exist yet. A test can instantiate a 128-bit one
// and prove the arithmetic now, which is the order this wants doing in.

template <int Capacity>
struct BitMaskValue
{
    static constexpr int capacity = Capacity;
    static constexpr int words = (Capacity + 63) / 64;

    std::array<uint64_t, (size_t) words> w{};

    static constexpr int wordOf(int id) { return id >> 6; }
    static constexpr uint64_t bitOf(int id) { return uint64_t{1} << (id & 63); }

    bool test(int id) const
    {
        if (id < 0 || id >= Capacity)
            return false;

        return (w[(size_t) wordOf(id)] & bitOf(id)) != 0;
    }

    bool any() const
    {
        for (auto word : w)
            if (word != 0)
                return true;

        return false;
    }

    BitMaskValue operator|(const BitMaskValue& other) const
    {
        BitMaskValue result;
        for (int i = 0; i < words; ++i)
            result.w[(size_t) i] = w[(size_t) i] | other.w[(size_t) i];

        return result;
    }

    // One id as a mask, for the callers that used to write 1ull << id inline.
    static BitMaskValue forParameter(int id)
    {
        BitMaskValue result;
        if (id >= 0 && id < Capacity)
            result.w[(size_t) wordOf(id)] = bitOf(id);

        return result;
    }

    // Every id at once, for the caller that used to write ~0ull.
    static BitMaskValue all()
    {
        BitMaskValue result;
        result.w.fill(~uint64_t{0});

        // The last word is partial whenever the capacity is not a multiple of
        // 64. Bits above the capacity can never be set, so leaving them on
        // would break nothing today - but this would stop being the union of
        // every id, and sooner or later something will compare against it.
        constexpr int spare = words * 64 - Capacity;
        if constexpr (spare > 0)
            result.w[(size_t) words - 1] = ~uint64_t{0} >> spare;

        return result;
    }
};

// The storage half. Written from the message thread and from MIDI, read from
// the audio thread, same as the single atomic it replaces.
//
// Sequentially consistent throughout, which is what the StepSequencer masks
// already used. One of the four call sites - the host-write drain - was
// relaxed; making it seq_cst is strictly stronger and costs a fence on a path
// that runs when somebody moves a control, not per sample.
//
// A load is no longer one instruction. It was not one before either:
// heldParameters() already ORed three separate atomic loads, so the code has
// always tolerated the three being read at slightly different instants. This
// makes it six loads rather than three - the same class of inconsistency, a
// little wider. What it costs is that a lane may play, or not, for one tick, on
// a dial somebody is physically moving. It is corrected on the next tick.
template <int Capacity>
class AtomicBitMask
{
public:
    using Value = BitMaskValue<Capacity>;

    static constexpr int capacity = Capacity;
    static constexpr int words = Value::words;

    void set(int id, bool on)
    {
        if (id < 0 || id >= Capacity)
            return;

        auto& word = w[(size_t) Value::wordOf(id)];
        const auto bit = Value::bitOf(id);

        if (on)
            word.fetch_or(bit);
        else
            word.fetch_and(~bit);
    }

    void merge(const Value& other)
    {
        for (int i = 0; i < words; ++i)
            w[(size_t) i].fetch_or(other.w[(size_t) i]);
    }

    void andNot(const Value& other)
    {
        for (int i = 0; i < words; ++i)
            w[(size_t) i].fetch_and(~other.w[(size_t) i]);
    }

    void clearAll()
    {
        for (auto& word : w)
            word.store(0);
    }

    Value load() const
    {
        Value result;
        for (int i = 0; i < words; ++i)
            result.w[(size_t) i] = w[(size_t) i].load();

        return result;
    }

    // Read and empty in one move, for the drain in processBlock. A bit set
    // between one word's exchange and the next is not lost - it is simply
    // picked up on the following block, against a countdown that already runs
    // down over thousands of them.
    Value exchangeZero()
    {
        Value result;
        for (int i = 0; i < words; ++i)
            result.w[(size_t) i] = w[(size_t) i].exchange(0);

        return result;
    }

private:
    std::array<std::atomic<uint64_t>, (size_t) words> w{};
};

// What the project actually uses. One word today; two the moment
// paramreg::maxParameters passes 64, with nothing else to change.
using ParameterMask = BitMaskValue<paramreg::maxParameters>;
using AtomicParameterMask = AtomicBitMask<paramreg::maxParameters>;
} // namespace wavelathe
