#include <iostream>
#define CPPHTTPLIB_OPENSSL_SUPPORT // Omit this line if compiling without SSL
#include "httplib.h"

using namespace std;

int main() {
    // Create an HTTP client for example.com on port 80
    httplib::Client cli("http://example.com:80");

    cout << "Sending HTTP GET request to example.com:80..." << endl;

    // Send GET request to path "/"
    auto res = cli.Get("/");

    // Check if request was successful
    if (res) {
        cout << "Status Code: " << res->status << endl;
        cout << "Content-Type: " << res->get_header_value("Content-Type") << endl;
        cout << "\nResponse Body:\n" << endl;
        cout << res->body << endl;
    } else {
        auto err = res.error();
        cerr << "HTTP request failed with error code: " << static_cast<int>(err) << endl;
    }

    return 0;
}