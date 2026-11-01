
#include <iostream>
#include <string>
#include <cstdint>
#include <ctime>
#include <pcap.h>

#define CAPTURE_DURATION 1
#define SNAP_LEN 65535
#define CAPTURE_TIMEOUT 1000

using namespace std;

/* Read a 16-bit big-endian value safely */
uint16_t read_be16(const u_char* p)
{
    return (static_cast<uint16_t>(p[0]) << 8) |
            static_cast<uint16_t>(p[1]);
}

/* Process and parse captured packets */
void process_packet(
    const struct pcap_pkthdr* header,
    const u_char* packet,
    int& packet_count)
{
    size_t len = header->caplen;
    size_t offset = 14;

    // Check minimum Ethernet header length
    if (len < 14)
        return;

    uint16_t ether_type = read_be16(packet + 12);

    // Handle VLAN-tagged Ethernet frames
    while (ether_type == 0x8100 ||
           ether_type == 0x88A8 ||
           ether_type == 0x9100)
    {
        if (len < offset + 4)
            return;

        ether_type = read_be16(packet + offset + 2);
        offset += 4;
    }

    // Only process IPv4 packets
    if (ether_type != 0x0800)
        return;

    // Check minimum IPv4 header length
    if (len < offset + 20)
        return;

    const u_char* ip = packet + offset;

    // Verify IPv4 version
    if ((ip[0] >> 4) != 4)
        return;

    // Calculate IP header length
    size_t ip_header_len = (ip[0] & 0x0F) * 4;

    if (ip_header_len < 20 ||
        len < offset + ip_header_len)
        return;

    // Verify TCP protocol
    if (ip[9] != 6)
        return;

    // Validate IPv4 total length
    uint16_t ip_total_len = read_be16(ip + 2);

    if (ip_total_len < ip_header_len + 20)
        return;

    if (len < offset + ip_total_len)
        return;

    // Ignore non-initial IPv4 fragments
    uint16_t fragment = read_be16(ip + 6);

    if ((fragment & 0x1FFF) != 0)
        return;

    // Locate TCP header
    const u_char* tcp = ip + ip_header_len;

    // Calculate TCP header length
    size_t tcp_header_len = (tcp[12] >> 4) * 4;

    if (tcp_header_len < 20 ||
        tcp_header_len > ip_total_len - ip_header_len)
        return;

    // Extract TCP ports
    uint16_t sport = read_be16(tcp);
    uint16_t dport = read_be16(tcp + 2);

    cout << "Captured packet #" << ++packet_count
         << " | Source Port: " << sport
         << " -> Destination Port: " << dport
         << endl;
}

int main()
{
    char errbuf[PCAP_ERRBUF_SIZE];

    pcap_t* handle = nullptr;
    pcap_if_t* alldevs = nullptr;

    struct bpf_program fp;

    bool filter_compiled = false;

    int packet_count = 0;

    bpf_u_int32 net = 0;
    bpf_u_int32 mask = PCAP_NETMASK_UNKNOWN;

    const char filter[] = "tcp port 80";

    // 1. Find available network interfaces
    if (pcap_findalldevs(&alldevs, errbuf) == -1)
    {
        cerr << "Device discovery error: "
             << errbuf << endl;
        return 2;
    }

    if (alldevs == nullptr)
    {
        cerr << "No network interfaces found." << endl;
        return 2;
    }

    // 2. Select the first available interface
    if (alldevs->name == nullptr)
    {
        cerr << "Invalid network interface." << endl;
        pcap_freealldevs(alldevs);
        return 2;
    }

    // Copy device name into a C++ string
    string dev = alldevs->name;

    // 3. Look up network and mask
    if (pcap_lookupnet(
            dev.c_str(), &net, &mask, errbuf) == -1)
    {
        cerr << "Network lookup warning: "
             << errbuf << endl;

        mask = PCAP_NETMASK_UNKNOWN;
    }

    // 4. Open packet capture session
    handle = pcap_open_live(
        dev.c_str(),
        SNAP_LEN,
        1,
        CAPTURE_TIMEOUT,
        errbuf
    );

    pcap_freealldevs(alldevs);
    alldevs = nullptr;

    if (handle == nullptr)
    {
        cerr << "Couldn't open device "
             << dev << ": " << errbuf << endl;
        return 2;
    }

    cout << "Device: " << dev << endl;
    cout << "Listening for TCP port 80 traffic for "
         << CAPTURE_DURATION << " seconds..." << endl;

    // 5. Check Ethernet data-link type
    if (pcap_datalink(handle) != DLT_EN10MB)
    {
        cerr << "Unsupported data-link type. "
             << "Ethernet required." << endl;

        pcap_close(handle);
        return 2;
    }

    // 6. Compile BPF filter
    if (pcap_compile(
            handle, &fp, filter, 1, mask) == -1)
    {
        cerr << "Filter compilation error: "
             << pcap_geterr(handle) << endl;

        pcap_close(handle);
        return 2;
    }

    filter_compiled = true;

    // 7. Install BPF filter
    if (pcap_setfilter(handle, &fp) == -1)
    {
        cerr << "Filter installation error: "
             << pcap_geterr(handle) << endl;

        pcap_freecode(&fp);
        pcap_close(handle);
        return 2;
    }

    // 8. Capture packets for 10 seconds
    time_t start_time = time(nullptr);

    while (difftime(time(nullptr), start_time)
           < CAPTURE_DURATION)
    {
        struct pcap_pkthdr* header = nullptr;
        const u_char* packet = nullptr;

        int result = pcap_next_ex(
            handle, &header, &packet
        );

        if (result == 0)
        {
            // Timeout: check timer again
            continue;
        }

        if (result == -1)
        {
            cerr << "Capture error: "
                 << pcap_geterr(handle) << endl;
            break;
        }

        if (result == -2)
        {
            // Capture stopped
            break;
        }

        if (result == 1)
        {
            process_packet(
                header,
                packet,
                packet_count
            );
        }
    }

    // 9. Clean up
    if (filter_compiled)
        pcap_freecode(&fp);

    pcap_close(handle);

    cout << "\nCapture finished." << endl;
    cout << "Total TCP packets captured: "
         << packet_count << endl;

    return 0;
}
