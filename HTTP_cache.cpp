#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <arpa/inet.h>
#include "HTTP_cache.hpp"

void HTTP_cache::add_header(const std::vector<HeaderEntry> newHeader) {
    if (this->Handle_Singleton_Header_conflict(newHeader) || this->Handle_Mutually_Exclusive_Headers(newHeader)) {
        return ; 
    }

    for (const auto& h : newHeader) {
        auto target = find_header(this->header, h.key);
        auto isSingleton = std::find_if(singletons.begin(), singletons.end(), [&h](const std::string& singleton_name) {
            return singleton_name == h.key; 
        });
        
        if (target != this->header.end() && isSingleton != singletons.end()) {
            target->value = h.value;
            std::cout << "a";
        } else {
            this->header.push_back(h);
        }
    }
    this->last_accessed = std::time(nullptr); // Fixed TIME_UTC macro issue
    this->access_count++;
}

void HTTP_cache::remove_header(std::string key) {
    auto target = find_header(this->header, key);
    if (target == this->header.end()) {
        return;
    }
    this->header.erase(target);
    this->last_accessed = std::time(nullptr);
}

bool HTTP_cache::Handle_Singleton_Header_conflict(const std::vector<HeaderEntry> newHeader) {
    for (size_t i = 0; i < newHeader.size(); ++i) {
        auto is_singleton = std::find(singletons.begin(), singletons.end(), newHeader[i].key);
        if (is_singleton != singletons.end()) {
            for (size_t j = i + 1; j < newHeader.size(); ++j) {
                if (newHeader[j].key == newHeader[i].key) {
                    return true; // Conflict: Duplicate singleton in the same batch!
                }
            }
        }
    }
    return false; // No batch conflicts
}

bool HTTP_cache::Handle_Mutually_Exclusive_Headers(const std::vector<HeaderEntry> newHeaders) {
    for (const auto& newHeader : newHeaders) {
        std::string incomingKey = to_lower(newHeader.key);
        std::string incomingValue = to_lower(newHeader.value);

        if (incomingKey == "cache-control") {
            bool has_no_store = (incomingValue.find("no-store") != std::string::npos);
            bool has_max_age  = (incomingValue.find("max-age") != std::string::npos);
            bool has_no_cache = (incomingValue.find("no-cache") != std::string::npos);
            bool has_immutable = (incomingValue.find("immutable") != std::string::npos);

            if (has_no_store && has_max_age) {
                return true;
            }
            if (has_no_cache && has_immutable) {
                return true;
            }
        }

        auto found_te = find_header(this->header, "transfer-encoding");
        if (incomingKey == "content-length" && found_te != this->header.end()) {
            return true;
        }
        
        if (incomingKey == "transfer-encoding" && incomingValue.find("chunked") != std::string::npos) {
            auto found_cl = find_header(this->header, "content-length");
            if (found_cl != this->header.end()) {
                this->header.erase(found_cl);
            }
        }

        auto found_inm = find_header(this->header, "if-none-match");
        auto found_ims = find_header(this->header, "if-modified-since");

        if (incomingKey == "if-modified-since" && found_inm != this->header.end()) {
            return true; 
        }
        
        if (incomingKey == "if-none-match" && found_ims != this->header.end()) {
            this->header.erase(found_ims);
        }

        auto found_creds = find_header(this->header, "access-control-allow-credentials", "true");
        if (incomingKey == "access-control-allow-origin" && incomingValue == "*") {
            if (found_creds != this->header.end()) {
                return true;
            }
        }

        if (incomingKey == "access-control-allow-credentials" && incomingValue == "true") {
            auto found_wildcard_origin = find_header(this->header, "access-control-allow-origin", "*");
            if (found_wildcard_origin != this->header.end()) {
                return true;
            }
        }

        if (incomingKey == "connection" && incomingValue == "close") {
            auto found_upgrade = find_header(this->header, "upgrade");
            if (found_upgrade != this->header.end()) {
                this->header.erase(found_upgrade);
            }
        }
    }
    return false;
}
void  HTTP_cache::update_body(std::vector<char>newbody){
    this->body= newbody;
}

bool HTTP_cache::FromJson(const rapidjson::Value& obj) {
    if (!obj.IsObject() || !obj.HasMember("IP_address")) {
        return false;
    }

    const auto& ip_address = obj["IP_address"];
    if (!ip_address.IsObject()) return false;

    if (ip_address.HasMember("IP") && ip_address["IP"].IsUint()) {
        IPv4 = ip_address["IP"].GetUint();
    }

    if (ip_address.HasMember("Host") && ip_address["Host"].IsObject()) {
        const auto& hostObj = ip_address["Host"];

        if (hostObj.HasMember("Host") && hostObj["Host"].IsString()) {
            host = hostObj["Host"].GetString();
        }

        if (hostObj.HasMember("HTTP") && hostObj["HTTP"].IsObject()) {
            const auto& httpFile = hostObj["HTTP"];

            if (httpFile.HasMember("access_count") && httpFile["access_count"].IsUint64()) {
                access_count = httpFile["access_count"].GetUint64();
            }

            if (httpFile.HasMember("last_accessed") && httpFile["last_accessed"].IsInt64()) {
                last_accessed = static_cast<std::time_t>(httpFile["last_accessed"].GetInt64());
            }
            if (httpFile.HasMember("Last_Modified") && httpFile["Last_Modified"].IsInt64()) {
                Last_Modified = static_cast<std::time_t>(httpFile["Last_Modified"].GetInt64());
            }
            if (httpFile.HasMember("Expiration_Time") && httpFile["Expiration_Time"].IsInt64()) {
                Expiration_Time = static_cast<std::time_t>(httpFile["Expiration_Time"].GetInt64());
            }

            if (httpFile.HasMember("header") && httpFile["header"].IsArray()) {
                header.clear();
                const auto& headerArray = httpFile["header"];
                for (rapidjson::SizeType i = 0; i < headerArray.Size(); ++i) {
                    header.push_back(HeaderEntry::FromJson(headerArray[i]));
                }
            }

            if (httpFile.HasMember("body") && httpFile["body"].IsString()) {
                const char* bodyStr = httpFile["body"].GetString();
                rapidjson::SizeType bodyLen = httpFile["body"].GetStringLength();
                body.assign(bodyStr, bodyStr + bodyLen);
            }
        }
    }

    return true;
}
rapidjson::Value HTTP_cache::ToJson(rapidjson::Document::AllocatorType& allocator) const {
    rapidjson::Value obj(rapidjson::kObjectType);

    // Convert uint32_t IPv4 back to string format ("192.168.1.10")
    struct in_addr addr;
    addr.s_addr = htonl(IPv4);
    std::string ip_str = inet_ntoa(addr);

    rapidjson::Value ipObj(rapidjson::kObjectType);
    ipObj.AddMember("IP", rapidjson::Value(ip_str.c_str(), allocator), allocator);

    rapidjson::Value hostInnerObj(rapidjson::kObjectType);
    hostInnerObj.AddMember("Host", rapidjson::Value(host.c_str(), allocator), allocator);

    rapidjson::Value httpObj(rapidjson::kObjectType);
    httpObj.AddMember("access_count", access_count, allocator);
    httpObj.AddMember("cache_size", static_cast<uint64_t>(body.size()), allocator);
    httpObj.AddMember("last_accessed", static_cast<int64_t>(last_accessed), allocator);
    httpObj.AddMember("Last_Modified", static_cast<int64_t>(Last_Modified), allocator);
    httpObj.AddMember("Expiration_Time", static_cast<int64_t>(Expiration_Time), allocator);

    // Headers array with Capitalized "Key" and "Value"
    rapidjson::Value headerArray(rapidjson::kArrayType);
    for (const auto& h : header) {
        rapidjson::Value hEntry(rapidjson::kObjectType);
        hEntry.AddMember("Key", rapidjson::Value(h.key.c_str(), allocator), allocator);
        hEntry.AddMember("Value", rapidjson::Value(h.value.c_str(), allocator), allocator);
        headerArray.PushBack(hEntry, allocator);
    }
    httpObj.AddMember("header", headerArray, allocator);

    std::string bodyStr(body.begin(), body.end());
    httpObj.AddMember("body", rapidjson::Value(bodyStr.c_str(), allocator), allocator);

    hostInnerObj.AddMember("HTTP", httpObj, allocator);
    ipObj.AddMember("Host", hostInnerObj, allocator);
    obj.AddMember("IP_address", ipObj, allocator);

    return obj;
}

// Fixed class name (HTTP_cache instead of HTTP_Cache), return type (bool), and parameter name (raw_data)
// --- Testing Functions ---

void test_header_entry_normalization() {
    std::cout << "Running test_header_entry_normalization..." << std::endl;
    HeaderEntry entry("Content-Type", "application/JSON; charset=UTF-8");
    assert(entry.key == "content-type");
    assert(entry.value == "application/JSON; charset=UTF-8"); 
    std::cout << "[PASSED] test_header_entry_normalization\n" << std::endl;
}

void test_http_cache_initialization() {
    std::cout << "Running test_http_cache_initialization..." << std::endl;
    HTTP_cache cache;
    assert(cache.header.empty()); 
    assert(cache.body.empty());
    std::cout << "[PASSED] test_http_cache_initialization\n" << std::endl;
}

void test_add_and_remove_headers() {
    std::cout << "Running test_add_and_remove_headers..." << std::endl;
    HTTP_cache cache;
    
    std::vector<HeaderEntry> new_headers = {
        {"Authorization", "Bearer Token123XYZ"},
        {"X-Custom-Header", "ValueWithCase"}
    };
    
    cache.add_header(new_headers);
    assert(cache.header.size() == 2); 
    assert(cache.header[0].key == "authorization");
    assert(cache.header[0].value == "Bearer Token123XYZ"); 
    
    cache.remove_header("AUTHORIZATION");
    assert(cache.header.size() == 1);
    assert(cache.header[0].key == "x-custom-header");
    std::cout << "[PASSED] test_add_and_remove_headers\n" << std::endl;
}

void test_singleton_conflict_handler() {
    std::cout << "Running test_singleton_conflict_handler..." << std::endl;
    
    HTTP_cache cache1;
    std::vector<HeaderEntry> valid_batch = {
        {"Content-Length", "500"},
        {"Host", "example.com"},
        {"Content-Type", "application/json"}
    };
    cache1.add_header(valid_batch);
    assert(cache1.header.size() == 3);

    HTTP_cache cache2;
    std::vector<HeaderEntry> duplicate_singleton_batch = {
        {"Content-Length", "500"},
        {"Content-Length", "200"} 
    };
    cache2.add_header(duplicate_singleton_batch);
    assert(cache2.header.empty()); 

    std::cout << "[PASSED] test_singleton_conflict_handler\n" << std::endl;
}

void test_mutually_exclusive_headers_conflict() {
    std::cout << "Running test_mutually_exclusive_headers_conflict..." << std::endl;
    
    HTTP_cache cache1;
    std::vector<HeaderEntry> bad_cache_control = {
        {"Cache-Control", "no-store, max-age=3600"}
    };
    cache1.add_header(bad_cache_control);
    assert(cache1.header.empty()); 

    HTTP_cache cache2;
    cache2.header.push_back(HeaderEntry("Transfer-Encoding", "chunked"));
    
    std::vector<HeaderEntry> incoming_content_length = {
        {"Content-Length", "1024"}
    };
    cache2.add_header(incoming_content_length);
    
    auto cl_target = find_header(cache2.header, "content-length");
    assert(cl_target == cache2.header.end());

    HTTP_cache cache3;
    cache3.header.push_back(HeaderEntry("If-None-Match", "\"xyz123\""));
    
    std::vector<HeaderEntry> incoming_ims = {
        {"If-Modified-Since", "Wed, 21 Oct 2015 07:28:00 GMT"}
    };
    cache3.add_header(incoming_ims);
    
    auto ims_target = find_header(cache3.header, "if-modified-since");
    assert(ims_target == cache3.header.end());

    std::cout << "[PASSED] test_mutually_exclusive_headers_conflict\n" << std::endl;
}

int main() {
    std::cout << "=== STARTING HTTP CACHE TESTS ===\n" << std::endl;
    try {
        test_header_entry_normalization();
        test_http_cache_initialization();
        test_add_and_remove_headers();
        test_singleton_conflict_handler();
        test_mutually_exclusive_headers_conflict();
        std::cout << "=== ALL TESTS PASSED SUCCESSFULLY ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED WITH EXCEPTION: " << e.what() << std::endl;
        return 1;
    }
}