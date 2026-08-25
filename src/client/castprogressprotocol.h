/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include "creature.h"

#include <framework/net/inputmessage.h>
#include <framework/stdext/exception.h>

namespace CastProgressProtocol
{
constexpr uint8_t Feature = 136;
constexpr uint8_t CreatureDataSubtype = 15;
static_assert(Feature == Otc::GameCastProgress);

enum class Action : uint8_t
{
    Start = 1,
    Cancel = 2,
};

struct Command
{
    Action action{ Action::Start };
    uint64_t id{ 0 };
    uint32_t durationMs{ 0 };
    uint32_t remainingMs{ 0 };
};

inline Command readCommand(const InputMessagePtr& msg)
{
    const auto action = static_cast<Action>(msg->getU8());
    switch (action) {
        case Action::Start:
            return { action, msg->getU64(), msg->getU32(), msg->getU32() };
        case Action::Cancel:
            return { action, msg->getU64(), 0, 0 };
        default:
            throw stdext::exception("[CastProgressProtocol::readCommand] Unknown action {}", static_cast<uint8_t>(action));
    }
}

inline std::optional<CastProgressWireState> readSnapshot(const InputMessagePtr& msg)
{
    const uint8_t active = msg->getU8();
    if (active == 0)
        return std::nullopt;
    if (active != 1)
        throw stdext::exception("[CastProgressProtocol::readSnapshot] Invalid active marker {}", active);

    return CastProgressWireState{ msg->getU64(), msg->getU32(), msg->getU32() };
}

inline void parseSnapshotTail(const InputMessagePtr& msg, const CreaturePtr& creature, const bool enabled,
                              const CastProgressClock::time_point now = CastProgressClock::now())
{
    if (!enabled)
        return;

    const auto snapshot = readSnapshot(msg);
    if (creature)
        creature->applyCastProgressSnapshot(snapshot, now);
}
} // namespace CastProgressProtocol
