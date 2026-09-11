#include "value_cache.h"

#include <ctime>

#include "esp_log.h"

static const char *TAG = "value_cache";

ValueCache &ValueCache::instance()
{
    static ValueCache cache;
    return cache;
}

ValueCacheEntry *ValueCache::find_or_claim(uint64_t node_id, uint16_t endpoint_id,
                                           uint32_t cluster_id, uint32_t attribute_id,
                                           bool create)
{
    for (auto &e : m_entries) {
        if (e.node_id == node_id && e.endpoint_id == endpoint_id &&
            e.cluster_id == cluster_id && e.attribute_id == attribute_id) {
            return &e;
        }
    }

    if (!create || m_entries.size() >= kMaxEntries) {
        if (create) {
            ESP_LOGW(TAG, "Cache full (%u entries); dropping %016llX/%u/%08lX/%08lX",
                     (unsigned)kMaxEntries, (unsigned long long)node_id, endpoint_id,
                     (unsigned long)cluster_id, (unsigned long)attribute_id);
        }
        return nullptr;
    }

    m_entries.push_back(ValueCacheEntry{node_id, endpoint_id, cluster_id, attribute_id,
                                        0, false, 0});
    return &m_entries.back();
}

void ValueCache::put(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                     uint32_t attribute_id, int64_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ValueCacheEntry *e = find_or_claim(node_id, endpoint_id, cluster_id, attribute_id, true);
    if (!e) {
        return;
    }

    e->value            = value;
    e->valid            = true;
    e->last_update_unix = (uint32_t)time(nullptr);
}

std::vector<ValueCacheEntry> ValueCache::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

bool ValueCache::get(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id,
                     uint32_t attribute_id, int64_t *out_value) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto &e : m_entries) {
        if (e.node_id == node_id && e.endpoint_id == endpoint_id &&
            e.cluster_id == cluster_id && e.attribute_id == attribute_id) {
            if (!e.valid) {
                return false;
            }
            if (out_value) {
                *out_value = e.value;
            }
            return true;
        }
    }
    return false;
}

void ValueCache::forget_node(uint64_t node_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        it = (it->node_id == node_id) ? m_entries.erase(it) : it + 1;
    }
}
