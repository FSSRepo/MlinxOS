#include "system/network.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#ifdef __linux__
#include "system/process.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace linweb {
namespace net {

namespace {

Result fail(const std::string& message) {
    Result r;
    r.error = message;
    return r;
}

Result success() {
    Result r;
    r.ok = true;
    return r;
}

std::string trim(const std::string& text) {
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = text.find_last_not_of(" \t\r\n");
    return text.substr(a, b - a + 1);
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool contains(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

} // namespace

// ---- parsing helpers (portable) -------------------------------------------------------------------

std::string unescape_wpa(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) { out += text[i]; continue; }
        char next = text[++i];
        if (next == 'x' && i + 2 < text.size() + 0 && hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        } else if (next == 'n') out += '\n';
        else if (next == 'r') out += '\r';
        else if (next == 't') out += '\t';
        else out += next;                       // \\ and \"
    }
    return out;
}

std::string hex_encode(const std::string& text) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}

std::map<std::string, std::string> parse_key_values(const std::string& text) {
    std::map<std::string, std::string> kv;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

int signal_percent(int dbm) {
    int percent = (dbm + 90) * 100 / 60;
    return std::max(0, std::min(100, percent));
}

bool parse_bss(const std::string& text, WifiNetwork& network) {
    std::map<std::string, std::string> kv = parse_key_values(text);
    auto ssid = kv.find("ssid");
    if (ssid == kv.end()) return false;
    network = WifiNetwork();
    network.ssid = unescape_wpa(ssid->second);
    // Hidden networks announce an empty or all-zero SSID.
    if (network.ssid.find_first_not_of(std::string(1, '\0')) == std::string::npos) return false;
    network.frequency = std::atoi(kv["freq"].c_str());
    network.signal_dbm = std::atoi(kv["level"].c_str());
    network.signal_percent = signal_percent(network.signal_dbm);
    const std::string& flags = kv["flags"];
    if (contains(flags, "SAE") && contains(flags, "PSK")) network.security = "WPA2/WPA3";
    else if (contains(flags, "SAE")) network.security = "WPA3";
    else if (contains(flags, "WPA2") || contains(flags, "RSN")) network.security = contains(flags, "EAP") ? "WPA2-Enterprise" : "WPA2";
    else if (contains(flags, "WPA")) network.security = "WPA";
    else if (contains(flags, "WEP")) network.security = "WEP";
    else network.security = "Open";
    network.secure = network.security != "Open";
    return true;
}

std::vector<WifiNetwork> collect_networks(const std::vector<std::string>& bss_replies, const std::string& connected_ssid) {
    std::vector<WifiNetwork> networks;
    for (const std::string& reply : bss_replies) {
        WifiNetwork n;
        if (!parse_bss(reply, n)) continue;
        bool merged = false;
        for (WifiNetwork& existing : networks) {
            if (existing.ssid != n.ssid) continue;
            if (n.signal_dbm > existing.signal_dbm) existing = n;
            merged = true;
            break;
        }
        if (!merged) networks.push_back(n);
    }
    for (WifiNetwork& n : networks) n.connected = !connected_ssid.empty() && n.ssid == connected_ssid;
    std::stable_sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.connected != b.connected) return a.connected;
        return a.signal_dbm > b.signal_dbm;
    });
    return networks;
}

std::string validate_connect(const std::string& ssid, const std::string& passphrase) {
    if (ssid.empty() || ssid.size() > 32) return "The network name must be 1 to 32 bytes long";
    if (passphrase.empty()) return "";                                   // open network
    for (unsigned char c : passphrase) {
        if (c < 0x20 || c == 0x7f) return "The password has characters that cannot be used";
    }
    if (passphrase.size() == 64) {
        for (char c : passphrase) {
            if (hex_value(c) < 0) return "A 64-character key must be hexadecimal";
        }
        return "";
    }
    if (passphrase.size() < 8 || passphrase.size() > 63) return "The password must have 8 to 63 characters";
    return "";
}

#ifdef __linux__

// ---- addresses ------------------------------------------------------------------------------------

std::vector<Address> local_addresses() {
    std::vector<Address> out;
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        char buf[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(it->ifa_addr)->sin_addr, buf, sizeof(buf))) continue;
        out.push_back({it->ifa_name, buf});
    }
    freeifaddrs(list);
    return out;
}

std::string ipv4_of(const std::string& iface) {
    for (const Address& a : local_addresses()) {
        if (a.iface == iface) return a.ip;
    }
    return std::string();
}

// ---- Wi-Fi ----------------------------------------------------------------------------------------

namespace {

std::string g_ctrl_dir = "/var/run/wpa_supplicant";
std::string g_sys_net = "/sys/class/net";
const char* kWpaConf = "/etc/wpa_supplicant.conf";

// Connection attempt being followed by wifi_status().
std::string g_target_ssid;
std::chrono::steady_clock::time_point g_started;
std::string g_failed_error;
pid_t g_dhcp_pid = 0;
int g_dhcp_attempts = 0;

// One request/reply exchange with wpa_supplicant over its UNIX datagram control socket.
bool ctrl_request(const std::string& iface, const std::string& command, std::string& reply, int timeout_ms = 3000) {
    reply.clear();
    static int counter = 0;
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un local;
    std::memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    std::snprintf(local.sun_path, sizeof(local.sun_path), "/tmp/linweb-wpa-%d-%d", static_cast<int>(getpid()), counter++);
    unlink(local.sun_path);
    bool ok = false;
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) == 0) {
        struct sockaddr_un dest;
        std::memset(&dest, 0, sizeof(dest));
        dest.sun_family = AF_UNIX;
        std::snprintf(dest.sun_path, sizeof(dest.sun_path), "%s/%s", g_ctrl_dir.c_str(), iface.c_str());
        if (connect(fd, reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest)) == 0 &&
            send(fd, command.data(), command.size(), 0) == static_cast<ssize_t>(command.size())) {
            struct pollfd pfd = {fd, POLLIN, 0};
            if (poll(&pfd, 1, timeout_ms) > 0) {
                char buf[16384];
                ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n >= 0) {
                    reply.assign(buf, static_cast<size_t>(n));
                    ok = true;
                }
            }
        }
    }
    unlink(local.sun_path);
    close(fd);
    return ok;
}

bool ctrl_ok(const std::string& iface, const std::string& command) {
    std::string reply;
    return ctrl_request(iface, command, reply) && trim(reply) == "OK";
}

void unblock_wifi() {
    DIR* dir = opendir("/sys/class/rfkill");
    if (!dir) return;
    while (struct dirent* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "rfkill", 6) != 0) continue;
        std::string base = std::string("/sys/class/rfkill/") + entry->d_name;
        std::ifstream type(base + "/type");
        std::string kind;
        std::getline(type, kind);
        if (trim(kind) != "wlan") continue;
        std::ofstream soft(base + "/soft");
        soft << "0";
    }
    closedir(dir);
}

void link_up(const std::string& iface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct ifreq req;
    std::memset(&req, 0, sizeof(req));
    std::snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", iface.c_str());
    if (ioctl(fd, SIOCGIFFLAGS, &req) == 0 && !(req.ifr_flags & IFF_UP)) {
        req.ifr_flags |= IFF_UP;
        ioctl(fd, SIOCSIFFLAGS, &req);
    }
    close(fd);
}

void stop_dhcp() {
    if (g_dhcp_pid > 0) {
        kill(g_dhcp_pid, SIGTERM);
        waitpid(g_dhcp_pid, nullptr, 0);
    }
    g_dhcp_pid = 0;
    g_dhcp_attempts = 0;
}

void flush_address(const std::string& iface) {
    std::string out;
    int code = 0;
    process::run_capture({"ip", "addr", "flush", "dev", iface}, out, code, 3000);
}

// Starts or follows udhcpc once the radio is associated and has no address yet.
void drive_dhcp(const std::string& iface) {
    if (g_dhcp_pid > 0) {
        int status = 0;
        pid_t done = waitpid(g_dhcp_pid, &status, WNOHANG);
        if (done == 0) return;                                  // still asking
        g_dhcp_pid = 0;
    }
    if (g_dhcp_attempts >= 4) {
        g_failed_error = "Connected to the network, but it did not give an IP address (DHCP)";
        return;
    }
    pid_t pid = process::spawn({"udhcpc", "-i", iface, "-n", "-q", "-t", "4", "-T", "2",
                                "-p", "/var/run/udhcpc." + iface + ".pid"}, false);
    if (pid < 0) {
        g_failed_error = "udhcpc is not installed in this system";
        return;
    }
    g_dhcp_pid = pid;
    ++g_dhcp_attempts;
}

bool supplicant_running(const std::string& iface) {
    std::string reply;
    return ctrl_request(iface, "PING", reply, 1000) && trim(reply) == "PONG";
}

} // namespace

std::string wifi_interface(const std::string& sys_net) {
    DIR* dir = opendir(sys_net.c_str());
    if (!dir) return std::string();
    std::vector<std::string> names;
    while (struct dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        if (stat((sys_net + "/" + name + "/wireless").c_str(), &st) == 0 ||
            stat((sys_net + "/" + name + "/phy80211").c_str(), &st) == 0) {
            names.push_back(name);
        }
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    return names.empty() ? std::string() : names.front();
}

void set_wpa_ctrl_dir(const std::string& dir) { g_ctrl_dir = dir; }
void set_sys_net_dir(const std::string& dir) { g_sys_net = dir; }

Result wifi_enable() {
    std::string iface = wifi_interface(g_sys_net);
    if (iface.empty()) return fail("No Wi-Fi adapter found (is the driver or its firmware missing?)");
    unblock_wifi();
    link_up(iface);
    if (supplicant_running(iface)) return success();
    if (process::find_tool("wpa_supplicant").empty()) return fail("wpa_supplicant is not installed in this system");
    struct stat st;
    if (stat(kWpaConf, &st) != 0) {
        std::ofstream conf(kWpaConf);
        conf << "ctrl_interface=" << g_ctrl_dir << "\nupdate_config=0\nap_scan=1\n";
        conf.close();
        chmod(kWpaConf, 0600);
    }
    std::string out;
    int code = 0;
    if (!process::run_capture({"wpa_supplicant", "-B", "-i", iface, "-c", kWpaConf, "-P", "/var/run/wpa_supplicant.pid"},
                              out, code, 8000) || code != 0) {
        return fail("wpa_supplicant did not start" + (trim(out).empty() ? std::string() : ": " + trim(out)));
    }
    for (int i = 0; i < 30; ++i) {                      // up to 3 s for the control socket
        if (supplicant_running(iface)) return success();
        usleep(100000);
    }
    return fail("wpa_supplicant is not answering");
}

Result wifi_scan() {
    Result enabled = wifi_enable();
    if (!enabled.ok) return enabled;
    std::string iface = wifi_interface(g_sys_net);
    std::string reply;
    if (!ctrl_request(iface, "SCAN", reply)) return fail("wpa_supplicant is not answering");
    reply = trim(reply);
    if (reply == "OK" || reply.find("BUSY") != std::string::npos) return success();   // BUSY: a scan is already running
    return fail("Scan refused: " + reply);
}

std::vector<WifiNetwork> wifi_networks() {
    std::vector<WifiNetwork> none;
    std::string iface = wifi_interface(g_sys_net);
    if (iface.empty()) return none;
    std::string status;
    std::string connected;
    if (ctrl_request(iface, "STATUS", status, 1500)) {
        auto kv = parse_key_values(status);
        if (kv["wpa_state"] == "COMPLETED") connected = unescape_wpa(kv["ssid"]);
    }
    std::vector<std::string> replies;
    for (int i = 0; i < 64; ++i) {                      // BSS <n> answers one access point at a time
        std::string reply;
        if (!ctrl_request(iface, "BSS " + std::to_string(i), reply, 1500) || trim(reply).empty()) break;
        replies.push_back(reply);
    }
    return collect_networks(replies, connected);
}

Result wifi_connect(const std::string& ssid, const std::string& passphrase) {
    std::string problem = validate_connect(ssid, passphrase);
    if (!problem.empty()) return fail(problem);
    Result enabled = wifi_enable();
    if (!enabled.ok) return enabled;
    std::string iface = wifi_interface(g_sys_net);

    stop_dhcp();
    if (!ipv4_of(iface).empty()) flush_address(iface);
    g_failed_error.clear();
    g_target_ssid = ssid;
    g_started = std::chrono::steady_clock::now();

    ctrl_ok(iface, "REMOVE_NETWORK all");
    std::string reply;
    if (!ctrl_request(iface, "ADD_NETWORK", reply)) return fail("wpa_supplicant is not answering");
    std::string id = trim(reply);
    if (id.empty() || !std::isdigit(static_cast<unsigned char>(id[0]))) return fail("wpa_supplicant refused the network");
    std::string prefix = "SET_NETWORK " + id + " ";
    bool ok = ctrl_ok(iface, prefix + "ssid " + hex_encode(ssid)) && ctrl_ok(iface, prefix + "scan_ssid 1");
    if (ok) {
        if (passphrase.empty()) ok = ctrl_ok(iface, prefix + "key_mgmt NONE");
        else if (passphrase.size() == 64) ok = ctrl_ok(iface, prefix + "psk " + passphrase);
        else ok = ctrl_ok(iface, prefix + "psk \"" + passphrase + "\"");
    }
    if (ok) ok = ctrl_ok(iface, "SELECT_NETWORK " + id);
    if (!ok) {
        g_target_ssid.clear();
        return fail("wpa_supplicant rejected the settings of this network");
    }
    return success();
}

Result wifi_disconnect() {
    std::string iface = wifi_interface(g_sys_net);
    if (iface.empty()) return fail("No Wi-Fi adapter found");
    g_target_ssid.clear();
    g_failed_error.clear();
    stop_dhcp();
    if (supplicant_running(iface)) {
        ctrl_ok(iface, "REMOVE_NETWORK all");
        ctrl_ok(iface, "DISCONNECT");
    }
    flush_address(iface);
    return success();
}

WifiStatus wifi_status() {
    WifiStatus st;
    std::string iface = wifi_interface(g_sys_net);
    if (iface.empty()) {
        st.error = "No Wi-Fi adapter found";
        return st;
    }
    st.available = true;
    st.iface = iface;
    st.ip = ipv4_of(iface);

    std::string reply;
    if (!ctrl_request(iface, "STATUS", reply, 1500)) {
        st.state = "OFF";
        return st;
    }
    auto kv = parse_key_values(reply);
    const std::string& wpa = kv["wpa_state"];
    st.ssid = unescape_wpa(kv["ssid"]);

    if (wpa == "COMPLETED") {
        if (!st.ip.empty()) {
            st.state = "CONNECTED";
            g_target_ssid.clear();
            g_failed_error.clear();
            g_dhcp_attempts = 0;
            g_dhcp_pid = 0;
            return st;
        }
        st.state = "OBTAINING_IP";
        drive_dhcp(iface);
    } else if (wpa == "SCANNING" || wpa == "ASSOCIATING" || wpa == "ASSOCIATED" || wpa == "AUTHENTICATING" ||
               wpa == "4WAY_HANDSHAKE" || wpa == "GROUP_HANDSHAKE") {
        st.state = "CONNECTING";
    } else {
        st.state = g_target_ssid.empty() ? "DISCONNECTED" : "CONNECTING";
    }

    if (!g_target_ssid.empty() && g_failed_error.empty()) {
        std::string list;
        if (ctrl_request(iface, "LIST_NETWORKS", list, 1500) && list.find("TEMP-DISABLED") != std::string::npos) {
            g_failed_error = "Could not connect: wrong password or the network refused the connection";
        } else if (std::chrono::steady_clock::now() - g_started > std::chrono::seconds(45)) {
            g_failed_error = "Timed out while connecting";
        }
        if (!g_failed_error.empty()) {
            ctrl_ok(iface, "REMOVE_NETWORK all");     // stop the retry loop so the user can try again
            ctrl_ok(iface, "DISCONNECT");
            stop_dhcp();
        }
    }
    if (!g_failed_error.empty() && st.ip.empty()) {
        st.state = "FAILED";
        st.error = g_failed_error;
        g_target_ssid.clear();
    }
    if (st.state == "CONNECTING" || st.state == "OBTAINING_IP") {
        if (st.ssid.empty()) st.ssid = g_target_ssid;
    }
    return st;
}

// ---- web server -----------------------------------------------------------------------------------

namespace {

pid_t g_httpd = 0;
int g_httpd_port = 0;

bool httpd_alive() {
    if (g_httpd <= 0) return false;
    int status = 0;
    pid_t done = waitpid(g_httpd, &status, WNOHANG);
    if (done == 0) return true;
    g_httpd = 0;
    return false;
}

} // namespace

Result webserver_start(int port) {
    if (port < 1 || port > 65535) return fail("The port must be between 1 and 65535");
    if (httpd_alive()) {
        if (port == g_httpd_port) return success();
        return fail("The web server is already running on port " + std::to_string(g_httpd_port));
    }
    if (process::find_tool("steward-httpd").empty()) return fail("steward-httpd is not installed in this system");
    pid_t pid = process::spawn({"steward-httpd", std::to_string(port)}, true);
    if (pid < 0) return fail("Could not start the web server");
    usleep(250000);                                     // it exits at once when it cannot listen
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid) return fail("The web server could not listen on port " + std::to_string(port) + " (already in use?)");
    g_httpd = pid;
    g_httpd_port = port;
    return success();
}

Result webserver_stop() {
    if (httpd_alive()) {
        kill(-g_httpd, SIGTERM);
        waitpid(g_httpd, nullptr, 0);
    }
    g_httpd = 0;
    g_httpd_port = 0;
    return success();
}

WebServerStatus webserver_status() {
    WebServerStatus st;
    st.running = httpd_alive();
    if (st.running) {
        st.port = g_httpd_port;
        st.addresses = local_addresses();
    }
    return st;
}

#else // !__linux__

std::vector<Address> local_addresses() { return {}; }
std::string ipv4_of(const std::string&) { return std::string(); }
std::string wifi_interface(const std::string&) { return std::string(); }
void set_wpa_ctrl_dir(const std::string&) {}
void set_sys_net_dir(const std::string&) {}
Result wifi_enable() { return fail("Wi-Fi is only supported on Linux"); }
Result wifi_scan() { return fail("Wi-Fi is only supported on Linux"); }
std::vector<WifiNetwork> wifi_networks() { return {}; }
Result wifi_connect(const std::string&, const std::string&) { return fail("Wi-Fi is only supported on Linux"); }
Result wifi_disconnect() { return fail("Wi-Fi is only supported on Linux"); }
WifiStatus wifi_status() { WifiStatus st; st.error = "Wi-Fi is only supported on Linux"; return st; }
Result webserver_start(int) { return fail("The web server is only supported on Linux"); }
Result webserver_stop() { return success(); }
WebServerStatus webserver_status() { return WebServerStatus(); }

#endif

} // namespace net
} // namespace linweb
