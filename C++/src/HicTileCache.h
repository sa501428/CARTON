#ifndef CARTON_HIC_TILE_CACHE_H
#define CARTON_HIC_TILE_CACHE_H

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "straw.h"

struct HicTileKey {
    std::string filePath;
    std::string matrixType;
    std::string norm;
    std::string unit;
    std::string chrX;
    std::string chrY;
    int32_t resolution = 0;
    int64_t x0 = 0;
    int64_t x1 = 0;
    int64_t y0 = 0;
    int64_t y1 = 0;

    bool operator==(const HicTileKey& other) const {
        return filePath == other.filePath &&
               matrixType == other.matrixType &&
               norm == other.norm &&
               unit == other.unit &&
               chrX == other.chrX &&
               chrY == other.chrY &&
               resolution == other.resolution &&
               x0 == other.x0 &&
               x1 == other.x1 &&
               y0 == other.y0 &&
               y1 == other.y1;
    }
};

struct HicTileKeyHash {
    std::size_t operator()(const HicTileKey& key) const {
        std::size_t h = 1469598103934665603ULL;
        auto mix = [&h](std::size_t v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        std::hash<std::string> stringHash;
        std::hash<int64_t> int64Hash;
        std::hash<int32_t> int32Hash;
        mix(stringHash(key.filePath));
        mix(stringHash(key.matrixType));
        mix(stringHash(key.norm));
        mix(stringHash(key.unit));
        mix(stringHash(key.chrX));
        mix(stringHash(key.chrY));
        mix(int32Hash(key.resolution));
        mix(int64Hash(key.x0));
        mix(int64Hash(key.x1));
        mix(int64Hash(key.y0));
        mix(int64Hash(key.y1));
        return h;
    }
};

struct HicTile {
    HicTileKey key;
    std::vector<contactRecord> records;
};

class HicTileCache {
public:
    explicit HicTileCache(std::size_t maxRecords = 1200000, std::size_t maxTiles = 24)
        : m_maxRecords(maxRecords), m_maxTiles(maxTiles) {}

    std::shared_ptr<const HicTile> get(const HicTileKey& key) {
        std::scoped_lock lock(m_mutex);
        auto found = m_entries.find(key);
        if (found == m_entries.end()) {
            return {};
        }
        m_lru.splice(m_lru.begin(), m_lru, found->second);
        return found->second->tile;
    }

    void put(HicTile tile) {
        std::scoped_lock lock(m_mutex);
        auto found = m_entries.find(tile.key);
        auto stored = std::make_shared<const HicTile>(std::move(tile));
        if (found != m_entries.end()) {
            m_recordCount -= found->second->tile->records.size();
            found->second->tile = std::move(stored);
            m_recordCount += found->second->tile->records.size();
            m_lru.splice(m_lru.begin(), m_lru, found->second);
            trimLocked();
            return;
        }

        m_recordCount += stored->records.size();
        m_lru.push_front(CacheEntry{std::move(stored)});
        m_entries[m_lru.front().tile->key] = m_lru.begin();
        trimLocked();
    }

    void clear() {
        std::scoped_lock lock(m_mutex);
        m_lru.clear();
        m_entries.clear();
        m_recordCount = 0;
    }

    std::size_t recordCount() const {
        std::scoped_lock lock(m_mutex);
        return m_recordCount;
    }

    std::size_t tileCount() const { std::scoped_lock lock(m_mutex); return m_lru.size(); }
    std::size_t maxRecords() const { std::scoped_lock lock(m_mutex); return m_maxRecords; }
    std::size_t maxTiles() const { std::scoped_lock lock(m_mutex); return m_maxTiles; }

    void setLimits(std::size_t maxRecords, std::size_t maxTiles) {
        std::scoped_lock lock(m_mutex);
        m_maxRecords = std::max<std::size_t>(1, maxRecords);
        m_maxTiles = std::max<std::size_t>(1, maxTiles);
        trimLocked();
    }

    void removeFile(const std::string& filePath) {
        std::scoped_lock lock(m_mutex);
        for (auto it = m_lru.begin(); it != m_lru.end();) {
            if (it->tile->key.filePath != filePath) {
                ++it;
                continue;
            }
            m_recordCount -= it->tile->records.size();
            m_entries.erase(it->tile->key);
            it = m_lru.erase(it);
        }
    }

private:
    struct CacheEntry {
        std::shared_ptr<const HicTile> tile;
    };

    void trimLocked() {
        while ((m_recordCount > m_maxRecords || m_lru.size() > m_maxTiles) && !m_lru.empty()) {
            auto& oldest = m_lru.back();
            m_recordCount -= oldest.tile->records.size();
            m_entries.erase(oldest.tile->key);
            m_lru.pop_back();
        }
    }

    std::size_t m_maxRecords = 0;
    std::size_t m_maxTiles = 0;
    std::size_t m_recordCount = 0;
    mutable std::mutex m_mutex;
    std::list<CacheEntry> m_lru;
    std::unordered_map<HicTileKey, std::list<CacheEntry>::iterator, HicTileKeyHash> m_entries;
};

#endif
