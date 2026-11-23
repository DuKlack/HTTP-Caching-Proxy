#ifndef HTTP_CACHE_HPP
#define HTTP_CACHE_HPP

#define MEMORYSIZE 1048576  // Example: 1 MB memory limit (adjust as needed)

#include <ctime>
#include <queue>
#include <vector>
#include <cstdint> 
#include <string>
#include <pthread.h>
#include <algorithm>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

inline std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return str;
}

static const std::vector<std::string> singletons = {
    "content-length", "host", "transfer-encoding", "content-type",
    "strict-transport-security", "access-control-allow-origin",
    "access-control-allow-credentials", "x-frame-options",
    "x-content-type-options", "authorization", "proxy-authorization",
    "location", "content-range", "content-disposition", "etag",
    "last-modified", "retry-after"
};

class HeaderEntry {
public:
    std::string key;
    std::string value;

    HeaderEntry(std::string nkey, std::string nvalue) {
        this->key = to_lower(nkey);
        this->value = nvalue;
    }

    rapidjson::Value ToJson(rapidjson::Document::AllocatorType& allocator) const {
        rapidjson::Value obj(rapidjson::kObjectType);
        
        rapidjson::Value jsonKey(key.c_str(), allocator);
        rapidjson::Value jsonValue(value.c_str(), allocator);
        
        obj.AddMember("key", jsonKey, allocator);
        obj.AddMember("value", jsonValue, allocator);
        
        return obj;
    }

    static HeaderEntry FromJson(const rapidjson::Value& json) {
        std::string k = (json.HasMember("key") && json["key"].IsString()) ? json["key"].GetString() : "";
        std::string v = (json.HasMember("value") && json["value"].IsString()) ? json["value"].GetString() : "";
        return HeaderEntry(k, v);
    }
};

inline std::vector<HeaderEntry>::iterator find_header(std::vector<HeaderEntry>& Header_list, std::string key, std::string value = "") {
    auto targetKey = to_lower(key);
    auto targetValue = to_lower(value);
    auto head = std::find_if(Header_list.begin(), Header_list.end(), [&targetKey, &targetValue](const HeaderEntry& h){
        bool keyMatch = (h.key == targetKey);
        bool valueMatch = targetValue.empty() || (to_lower(h.value).find(targetValue) != std::string::npos);
        return keyMatch && valueMatch;
    });
    
    return head;
}

class HTTP_cache {
public: 
    uint32_t id;
    uint32_t IPv4;
    std::string host;

    std::vector<HeaderEntry> header; // Does not need request 
    std::vector<char> body;
    
    std::time_t last_accessed;
    std::time_t Last_Modified;    
    std::time_t Expiration_Time;    

    uint64_t access_count = 0; // Hit counter      

    void add_header(const std::vector<HeaderEntry>& newHeader);
    void remove_header(const std::string& key);
    bool Handle_Singleton_Header_conflict(const std::vector<HeaderEntry>& newHeader)const;
    bool Handle_Mutually_Exclusive_Headers(const std::vector<HeaderEntry>& newHeader);
    void update_body(std::vector<char>newbody);
    rapidjson::Value ToJson(rapidjson::Document::AllocatorType& allocator) const;
    bool FromJson(const rapidjson::Value& obj); // Added missing semicolon here
    void update_TTL(std::time_t ttl_seconds = 300);
     bool is_cacheable(std::time_t& out_ttl_seconds, std::time_t default_ttl = 300) const;
};

#endif 