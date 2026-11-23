#ifndef CACHE_HPP
#define CACHE_HPP

#define LOG_DESTINATION "Cache.json"
#define BUFFER_SIZE 5

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include "HTTP_cache.hpp"

struct CacheKey {
    uint32_t IPv4 = 0;
    std::string host;

    bool operator==(const CacheKey& other) const {
        return IPv4 == other.IPv4 && host == other.host;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& k) const {
        std::size_t h1 = std::hash<uint32_t>{}(k.IPv4);
        std::size_t h2 = std::hash<std::string>{}(k.host);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

class Cache {
public:
    std::unordered_map<CacheKey, HTTP_cache, CacheKeyHash> buffer;

    std::optional<HTTP_cache> Find_in_Queue(uint32_t target_ip, const std::string& Host);
    std::optional<HTTP_cache> Find_in_Log(uint32_t target_ip, const std::string& Host);
    std::optional<HTTP_cache> TTL_Check();
    void add_to_queue(const HTTP_cache& cache);
    void remove_from_queue(const HTTP_cache& cache);
    void write_to_log(const HTTP_cache& cache);
    bool update_cache(const HTTP_cache& cache);

private:
    std::recursive_mutex cache_mutex;

    HTTP_cache* TTL_Check_Internal() {
        std::time_t current_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        for (auto& [key, lookupcache] : buffer) {
            if (lookupcache.Expiration_Time > 0 && lookupcache.Expiration_Time < current_time) {
                return &lookupcache;
            }
        }
        return nullptr;
    }

    
    HTTP_cache* Find_min() {
        HTTP_cache* min_item = nullptr;
        uint64_t min_access = UINT64_MAX;

        for (auto& [key, lookupcache] : buffer) {
            if (lookupcache.access_count < min_access) {
                min_access = lookupcache.access_count;
                min_item = &lookupcache;
            }
        }
        return min_item;
    }
};

#endif