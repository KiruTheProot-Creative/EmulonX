// Emulon X — keeps shared objects alive until the audio thread can no longer be
// using them, then frees them on the message thread. This way the audio thread
// never deallocates memory (which could cause clicks and dropouts).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace emx
{
class ReleasePool
{
public:
    template <typename T>
    void add (const std::shared_ptr<T>& p)
    {
        if (p == nullptr)
            return;
        const juce::ScopedLock sl (lock);
        for (auto& existing : pool)
            if (existing.get() == p.get())
                return;
        pool.emplace_back (p);
    }

    /** Frees everything only the pool still references. Call from the message thread. */
    void collect()
    {
        const juce::ScopedLock sl (lock);
        pool.erase (std::remove_if (pool.begin(), pool.end(), [] (const auto& p) { return p.use_count() <= 1; }), pool.end());
    }

    size_t size() const
    {
        const juce::ScopedLock sl (lock);
        return pool.size();
    }

private:
    juce::CriticalSection lock;
    std::vector<std::shared_ptr<const void>> pool;
};
} // namespace emx
