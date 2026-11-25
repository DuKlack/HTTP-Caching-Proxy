# C++ HTTP Caching Proxy

A multi-threaded HTTP/1.1 caching proxy server in C++17 with an in-memory LFU/TTL cache, JSON disk persistence (`Cache.log`), `Cache-Control` support, and HTTPS `CONNECT` tunneling.

## Features

- **Concurrent Client Handling**: Spawns a detached thread per incoming client connection on port `8080`.
- **HTTP Caching (`GET` / `200 OK`)**:
  - In-memory cache queue (`BUFFER_SIZE = 5`) with LFU eviction and TTL expiration.
  - Persistent JSON cache log (`Cache.log`) with automatic deduplication and expired-entry pruning.
  - Honors `Cache-Control` directives (`no-store`, `no-cache`, `private`, and `max-age`).
- **Header Validation**: Normalizes header keys to lowercase and rejects duplicate singleton headers or mutually exclusive header combinations (e.g., `Content-Length` with `Transfer-Encoding: chunked`).
- **HTTPS `CONNECT` Tunneling**: Bidirectional `poll`-based TCP tunneling for HTTPS traffic.
- **Request Body & Keep-Alive Handling**: Reads `Content-Length` request bodies, rewrites `http://` proxy targets to origin-form, and forces `Connection: close` on upstream requests.

## Project Structure

- `ox.cpp` — Proxy server entry point (`main`), socket setup, `handle_client`, request parsing, forwarding, and `CONNECT` tunneling.
- `Cache.hpp` / `Cache.cpp` — Thread-safe `Cache` class managing the in-memory queue (`Find_in_Queue`, `add_to_queue`, `TTL_Check`, `update_cache`) and JSON log (`Find_in_Log`, `write_to_log`).
- `HTTP_cache.hpp` / `HTTP_cache.cpp` — `HTTP_cache` and `HeaderEntry` classes, header conflict validation, TTL helpers, JSON serialization, and unit tests (`HTTP_CACHE_RUN_TESTS`).

## Prerequisites

- **C++17** compiler (`g++`, `clang++`, or MSVC)
- **RapidJSON** headers (`rapidjson-dev` on Debian/Ubuntu, or `rapidjson` via Homebrew/vcpkg)

## Build & Run

Build the proxy:

```bash
g++ -std=c++17 ox.cpp Cache.cpp HTTP_cache.cpp -pthread -o proxy
./proxy
```

*(If your entry file is named `main.cpp`, replace `ox.cpp` with `main.cpp`.)*

Build and run the unit tests standalone:

```bash
g++ -std=c++17 -DHTTP_CACHE_RUN_TESTS HTTP_cache.cpp -o test_cache
./test_cache
```

## Testing the Proxy

Start `./proxy` (listens on `0.0.0.0:8080`), then send requests in another terminal:

```bash
# Plain HTTP GET (1st request = [MISS], 2nd request = [HIT-QUEUE])
curl -H "Accept-Encoding: identity" -x http://localhost:8080 http://example.com
curl -H "Accept-Encoding: identity" -x http://localhost:8080 http://httpforever.com/

# Cache-Control max-age & POST forwarding
curl -x http://localhost:8080 "http://httpbin.org/response-headers?Cache-Control=max-age%3D60"
curl -x http://localhost:8080 -X POST -d '{"test":1}' -H "Content-Type: application/json" http://httpbin.org/post

# HTTPS CONNECT tunnel
curl -x http://localhost:8080 https://example.com
```

## `Cache.log` JSON Format

Cached responses are persisted to `Cache.log` in the following JSON structure:

```json
{
  "IP_address": {
    "IP": "192.168.1.10",
    "Host": {
      "Host": "example.com",
      "HTTP": {
        "access_count": 5,
        "cache_size": 1024,
        "last_accessed": 1718500000,
        "Last_Modified": 1718400000,
        "Expiration_Time": 1718600000,
        "header": [
          { "Key": "content-type", "Value": "application/json" }
        ],
        "body": "Your response body text here..."
      }
    }
  }
}
```