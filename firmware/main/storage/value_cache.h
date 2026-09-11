#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>
#include <vector>

// Generic in-memory cache of the latest value of an attribute, keyed by
// node + endpoint + cluster + attribute. Ported from matter-esp32-home-energy-manager.
//
// Matter subscriptions only report on change, so this holds the most recent value
// between reports. The history logger polls it on a timer rather than logging on each
// report, which keeps a stalled or bursty subscription from leaving gaps in the record,
// and keeps SD I/O off the CHIP event loop.
//
// This class is deliberately not Matter-aware.

struct ValueCacheEntry {
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    int64_t  value;             // raw attribute value as delivered (e.g. mW, mV, mA)
    bool     valid;             // false until first real update
    uint32_t last_update_unix;  // time of last put() for this entry
};

class ValueCache {
public:
    static ValueCache &instance();

    // Upsert the value for a key. Marks the entry valid and stamps last_update_unix.
    void put(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
             uint32_t attribute_id, int64_t value);

    // Copy of all entries.
    std::vector<ValueCacheEntry> snapshot() const;

    // Look up one key. Returns false if absent or never updated.
    bool get(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
             uint32_t attribute_id, int64_t *out_value) const;

    // Drop every entry for a node, so a removed device stops being sampled.
    void forget_node(uint64_t node_id);

private:
    ValueCache() = default;
    ValueCache(const ValueCache &) = delete;
    ValueCache &operator=(const ValueCache &) = delete;

    // Caller holds m_mutex.
    ValueCacheEntry *find_or_claim(uint64_t node_id, uint16_t endpoint_id,
                                   uint32_t cluster_id, uint32_t attribute_id, bool create);

    // 32 endpoints x 3 ElectricalPowerMeasurement attributes, matching the reference.
    static constexpr size_t kMaxEntries = 96;

    mutable std::mutex           m_mutex;
    std::vector<ValueCacheEntry> m_entries;
};
