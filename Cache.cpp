#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <mutex>
#include <optional>
#include <string>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include "HTTP_cache.hpp"
#include "Cache.hpp"


std::vector<HTTP_cache> load_all_from_log() {
    std::vector<HTTP_cache> entries;
    std::ifstream log_file(LOG_DESTINATION);
    if (!log_file.is_open()) {
        return entries;
    }

    std::ostringstream ss;
    ss << log_file.rdbuf();
    std::string content = ss.str();
    if (content.empty()) {
        return entries;
    }

    rapidjson::Document doc;
    doc.Parse(content.c_str());
    if (!doc.HasParseError()) {
        if (doc.IsObject()) {
            HTTP_cache item;
            if (item.FromJson(doc)) {
                entries.push_back(std::move(item));
            }
            return entries;
        }
        if (doc.IsArray()) {
            for (rapidjson::SizeType i = 0; i < doc.Size(); ++i) {
                HTTP_cache item;
                if (item.FromJson(doc[i])) {
                    entries.push_back(std::move(item));
                }
            }
            return entries;
        }
    }

    // Fallback: line-delimited JSON (NDJSON)
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        rapidjson::Document line_doc;
        line_doc.Parse(line.c_str());
        if (!line_doc.HasParseError() && line_doc.IsObject()) {
            HTTP_cache item;
            if (item.FromJson(line_doc)) {
                entries.push_back(std::move(item));
            }
        }
    }
    return entries;
}

std::optional<HTTP_cache> Cache::Find_in_Log(uint32_t target_ip, const std::string& Host) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    std::ifstream log_file(LOG_DESTINATION);
    if (!log_file.is_open()) {
        return std::nullopt;
    }

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::optional<HTTP_cache> latest_match;
    std::string line;
    while (std::getline(log_file, line)) {
        if (line.empty()) continue;

        rapidjson::Document doc;
        doc.Parse(line.c_str());

        if (doc.HasParseError() || !doc.IsObject()) {
            continue;
        }

        HTTP_cache cached_item;
        if (cached_item.FromJson(doc)) {
            if (cached_item.IPv4 == target_ip && cached_item.host == Host) {
                if (cached_item.Expiration_Time == 0 || cached_item.Expiration_Time >= now) {
                    latest_match = std::move(cached_item);
                } else {
                    latest_match.reset();
                }
            }
        }
    }

    return latest_match;
}

void Cache::write_to_log(const HTTP_cache& cache) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    rapidjson::Document doc;
    rapidjson::Value val = cache.ToJson(doc.GetAllocator());

    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    val.Accept(writer);

    std::ofstream log_file(LOG_DESTINATION, std::ios::app);
    if (log_file.is_open()) {
        log_file << sb.GetString() << '\n';
    }
}

std::optional<HTTP_cache> Cache::Find_in_Queue(uint32_t target_ip, const std::string& Host) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    auto it = buffer.find(CacheKey{target_ip, Host});
    if (it == buffer.end()) {
        return std::nullopt;
    }

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    if (it->second.Expiration_Time > 0 && it->second.Expiration_Time < now) {
        buffer.erase(it);
        return std::nullopt;
    }

    it->second.access_count++;
    it->second.last_accessed = now;
    return it->second;
}

std::optional<HTTP_cache> Cache::TTL_Check() {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    HTTP_cache* expired = TTL_Check_Internal();
    if (expired == nullptr) {
        return std::nullopt;
    }

    HTTP_cache expired_copy = *expired;
    buffer.erase(CacheKey{expired_copy.IPv4, expired_copy.host});
    return expired_copy;
}

void Cache::remove_from_queue(const HTTP_cache& cache) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);
    buffer.erase(CacheKey{cache.IPv4, cache.host});
}

void Cache::add_to_queue(const HTTP_cache& cache) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    CacheKey key{cache.IPv4, cache.host};
    auto existing = buffer.find(key);
    if (existing != buffer.end()) {
        existing->second = cache;
        return;
    }

    if (buffer.size() >= BUFFER_SIZE) {
        HTTP_cache* evict_target = TTL_Check_Internal();
        if (evict_target == nullptr) {
            evict_target = Find_min();
        }
        if (evict_target != nullptr) {
            buffer.erase(CacheKey{evict_target->IPv4, evict_target->host});
        }
    }

    buffer.emplace(std::move(key), cache);
}

bool Cache::update_cache(const HTTP_cache& cache) {
    std::lock_guard<std::recursive_mutex> lock(cache_mutex);

    auto it = buffer.find(CacheKey{cache.IPv4, cache.host});
    if (it == buffer.end()) {
        return false;
    }

    it->second.add_header(cache.header);
    it->second.update_body(cache.body);
    it->second.update_TTL();
    return true;
}