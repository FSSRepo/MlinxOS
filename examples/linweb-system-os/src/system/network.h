#pragma once
#include <map>
#include <string>
#include <vector>

namespace linweb {
namespace net {

struct Result {
    bool ok = false;
    std::string error;
};

struct Address {
    std::string iface;   // e.g. "wlan0"
    std::string ip;      // dotted IPv4
};

// IPv4 addresses of the non-loopback interfaces that are up.
std::vector<Address> local_addresses();
std::string ipv4_of(const std::string& iface);

// ---- Wi-Fi: wpa_supplicant driven through its control socket, DHCP through udhcpc ----------------

struct WifiNetwork {
    std::string ssid;
    int signal_dbm = 0;
    int signal_percent = 0;    // 0..100
    int frequency = 0;         // MHz
    bool secure = false;
    std::string security;      // "WPA2", "WPA3", "WPA", "WEP" or "Open"
    bool connected = false;
};

struct WifiStatus {
    bool available = false;    // a Wi-Fi adapter exists
    std::string iface;
    // OFF (supplicant not running), DISCONNECTED, CONNECTING, OBTAINING_IP, CONNECTED, FAILED
    std::string state = "OFF";
    std::string ssid;
    std::string ip;
    std::string error;
};

// Name of the first wireless interface under sys_net (a directory with a "wireless" or "phy80211" entry).
std::string wifi_interface(const std::string& sys_net = "/sys/class/net");

// Where wpa_supplicant keeps its control sockets (changed by tests).
void set_wpa_ctrl_dir(const std::string& dir);
// Where the interfaces are looked up (changed by tests).
void set_sys_net_dir(const std::string& dir);

Result wifi_enable();                                   // rfkill, link up, start wpa_supplicant
Result wifi_scan();                                     // asks for a new scan; results show up in ~2-3 s
std::vector<WifiNetwork> wifi_networks();               // scan results, strongest first, one per SSID
Result wifi_connect(const std::string& ssid, const std::string& passphrase);   // empty passphrase = open network
Result wifi_disconnect();
WifiStatus wifi_status();                               // polled by the UI; also drives DHCP

// ---- parsing helpers (exposed for tests) ----------------------------------------------------------
std::string unescape_wpa(const std::string& text);                 // \xNN, \\, \", \n
std::string hex_encode(const std::string& text);
std::map<std::string, std::string> parse_key_values(const std::string& text);
// One reply of the supplicant's "BSS <n>" command -> network. False when it has no usable SSID.
bool parse_bss(const std::string& text, WifiNetwork& network);
// Merges BSS replies into one entry per SSID (the strongest), strongest first.
std::vector<WifiNetwork> collect_networks(const std::vector<std::string>& bss_replies, const std::string& connected_ssid);
int signal_percent(int dbm);                                       // -90 dBm -> 0, -30 dBm -> 100
std::string validate_connect(const std::string& ssid, const std::string& passphrase);   // error text or ""

// ---- web server (steward-httpd) -------------------------------------------------------------------

struct WebServerStatus {
    bool running = false;
    int port = 0;
    std::string error;
    std::vector<Address> addresses;   // where it can be reached
};

Result webserver_start(int port);
Result webserver_stop();
WebServerStatus webserver_status();

} // namespace net
} // namespace linweb
