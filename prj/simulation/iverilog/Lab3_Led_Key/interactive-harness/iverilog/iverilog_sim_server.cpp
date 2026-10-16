// Portable VPI TCP Simulation Server
//
// Implements the same null-byte-delimited JSON protocol used by
// tests/cxxrtl/cxxrtl_sim_server.cpp and tests/verilator/verilator_sim_server.cpp,
// but attaches to VCS, Icarus Verilog, ModelSim or Questa through VPI callbacks.
//
// Build shape:
//   g++ -std=c++11 -O2 -fPIC -shared -Itests/cxxrtl -I$VCS_HOME/include \
//       -DVCS_REMOTE_TOP_SCOPE=top -DVCS_REMOTE_DISPLAY_SCOPE=top \
//       tests/vcs/vcs_sim_server.cpp -o libvcs_remote.so -lpthread
//   vcs -full64 -debug_all -load \
//       ./libvcs_remote.so:register_vcs_remote_callbacks ... -top top -o simv
//
// The linked simv prints PORT:<port> to stderr during start-of-simulation,
// then accepts one Remote client. The HDL simulator owns time; run_simulation
// waits/samples until the requested simulator time is reached.

#include "vpi_user.h"

// Icarus Verilog 11 omits these IEEE VPI relationship constants.
#ifndef vpiHighConn
#define vpiHighConn 76
#endif

#ifndef vpiLowConn
#define vpiLowConn 80
#endif

#define XSTR_(x) #x
#define XSTR(x) XSTR_(x)

#ifndef VCS_REMOTE_TOP_SCOPE
#define VCS_REMOTE_TOP_SCOPE TOP
#endif

#ifndef VCS_REMOTE_DISPLAY_SCOPE
#define VCS_REMOTE_DISPLAY_SCOPE VCS_REMOTE_TOP_SCOPE
#endif

#ifndef VCS_REMOTE_TIME_UNIT_FS
#define VCS_REMOTE_TIME_UNIT_FS 0ULL
#endif

#ifndef DIDE_REMOTE_LIVE_STIMULUS
#define DIDE_REMOTE_LIVE_STIMULUS 1
#endif

#include "nlohmann/json.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using json = nlohmann::json;

static const uint64_t FEMTOS_PER_SECOND = 1000000000000000ULL;

static std::mutex g_send_mutex;
static std::atomic<bool> g_server_stop(false);
static std::atomic<uint64_t> g_active_run_id(0);
static socket_t g_client = INVALID_SOCK;
static std::thread g_server_thread;

static void publish_port_file(int port) {
    const char* path = std::getenv("DIDE_REMOTE_PORT_FILE");
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return;
    }
    output << port << '\n';
    output.flush();
}

static uint64_t parse_timestamp(const std::string& s) {
    auto dot = s.find('.');
    if (dot == std::string::npos) {
        return 0;
    }
    uint64_t secs = std::stoull(s.substr(0, dot));
    std::string frac = s.substr(dot + 1);
    while (frac.size() < 15) {
        frac += '0';
    }
    frac = frac.substr(0, 15);
    return secs * FEMTOS_PER_SECOND + std::stoull(frac);
}

static std::string format_timestamp(uint64_t femtos) {
    uint64_t secs = femtos / FEMTOS_PER_SECOND;
    uint64_t frac = femtos % FEMTOS_PER_SECOND;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%llu.%015llu",
                  (unsigned long long)secs,
                  (unsigned long long)frac);
    return std::string(buf);
}

static uint64_t simulator_tick_fs() {
    if (VCS_REMOTE_TIME_UNIT_FS != 0ULL) {
        return VCS_REMOTE_TIME_UNIT_FS;
    }
    int precision = vpi_get(vpiTimePrecision, nullptr);
    int exponent = precision + 15;
    uint64_t scale = 1;
    while (exponent > 0) {
        if (scale > std::numeric_limits<uint64_t>::max() / 10ULL) {
            return 1;
        }
        scale *= 10ULL;
        --exponent;
    }
    return scale;
}

static uint64_t ticks_to_fs(uint64_t ticks) {
    uint64_t tick_fs = simulator_tick_fs();
    if (ticks > std::numeric_limits<uint64_t>::max() / tick_fs) {
        return std::numeric_limits<uint64_t>::max();
    }
    return ticks * tick_fs;
}

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len) {
            n |= (uint32_t)data[i + 1] << 8;
        }
        if (i + 2 < len) {
            n |= (uint32_t)data[i + 2];
        }
        result += b64_table[(n >> 18) & 0x3F];
        result += b64_table[(n >> 12) & 0x3F];
        result += (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? b64_table[n & 0x3F] : '=';
    }
    return result;
}

static bool base64_decode(const std::string& text,
                          std::vector<uint8_t>& output) {
    if (text.size() % 4 != 0) return false;
    auto decode = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    output.clear();
    for (size_t offset = 0; offset < text.size(); offset += 4) {
        int a = decode(text[offset]);
        int b = decode(text[offset + 1]);
        int c = text[offset + 2] == '=' ? -2 : decode(text[offset + 2]);
        int d = text[offset + 3] == '=' ? -2 : decode(text[offset + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) ||
            ((c == -2 || d == -2) && offset + 4 != text.size())) {
            return false;
        }
        uint32_t value = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        if (c >= 0) value |= (uint32_t)c << 6;
        if (d >= 0) value |= (uint32_t)d;
        output.push_back((uint8_t)(value >> 16));
        if (c >= 0) output.push_back((uint8_t)(value >> 8));
        if (d >= 0) output.push_back((uint8_t)value);
    }
    return true;
}

static void send_message(socket_t sock, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_send_mutex);
    std::string buf = msg;
    buf.push_back('\0');
    const char* p = buf.data();
    size_t remaining = buf.size();
    while (remaining > 0) {
        int sent = send(sock, p, (int)remaining, 0);
        if (sent <= 0) {
            break;
        }
        p += sent;
        remaining -= (size_t)sent;
    }
}

static json request_id(const json& request) {
    if (request.contains("request_id") && request["request_id"].is_string()) {
        return request["request_id"];
    }
    return nullptr;
}

static void send_response(socket_t sock, const json& request, json response) {
    response["request_id"] = request_id(request);
    send_message(sock, response.dump());
}

static void send_error(socket_t sock, const json& request,
                       const std::string& code, const std::string& message) {
    send_message(sock, json({
        {"type", "error"},
        {"request_id", request_id(request)},
        {"code", code},
        {"message", message},
    }).dump());
}

static bool recv_message(socket_t sock,
                         std::string& out,
                         std::vector<char>& read_buf) {
    for (;;) {
        auto it = std::find(read_buf.begin(), read_buf.end(), '\0');
        if (it != read_buf.end()) {
            out.assign(read_buf.begin(), it);
            read_buf.erase(read_buf.begin(), it + 1);
            return true;
        }

        char tmp[4096];
        int n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;
        }
        read_buf.insert(read_buf.end(), tmp, tmp + n);
    }
}

static socket_t open_listener(int& port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCK) {
        return INVALID_SOCK;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
#ifdef VCS_REMOTE_BIND_ANY
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
#else
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif
    addr.sin_port = 0;

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close_socket(listen_sock);
        return INVALID_SOCK;
    }

    socklen_t addr_len = sizeof(addr);
    getsockname(listen_sock, (struct sockaddr*)&addr, &addr_len);
    port = ntohs(addr.sin_port);

    if (listen(listen_sock, 1) != 0) {
        close_socket(listen_sock);
        return INVALID_SOCK;
    }

    return listen_sock;
}

static std::string replace_char(std::string value, char from, char to) {
    std::replace(value.begin(), value.end(), from, to);
    return value;
}

static bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

static bool contains_dot_or_space(const std::string& value) {
    return value.find('.') != std::string::npos ||
           value.find(' ') != std::string::npos;
}

struct SignalInfo {
    std::string display_name;
    std::string vpi_name;
    vpiHandle handle = nullptr;
    uint32_t width = 1;
    size_t num_chunks = 1;
    int direction = vpiNoDirection;
    bool writable = false;
};

struct TrackedSignal {
    std::string name;
    SignalInfo* signal = nullptr;
};

struct Sample {
    uint64_t time_fs = 0;
    std::vector<uint32_t> item_values;
};

struct StimulusEvent {
    uint64_t time_fs = 0;
    std::string port;
    uint32_t width = 0;
    std::vector<uint32_t> chunks;
    uint64_t sequence = 0;
};

struct StimulusCompare {
    bool operator()(const StimulusEvent& a, const StimulusEvent& b) {
        if (a.time_fs != b.time_fs) return a.time_fs > b.time_fs;
        return a.sequence > b.sequence;
    }
};

static json encode_sample_json(const Sample& s) {
    std::vector<uint8_t> raw;
    raw.reserve(s.item_values.size() * 4);
    for (uint32_t v : s.item_values) {
        raw.push_back((uint8_t)(v & 0xFF));
        raw.push_back((uint8_t)((v >> 8) & 0xFF));
        raw.push_back((uint8_t)((v >> 16) & 0xFF));
        raw.push_back((uint8_t)((v >> 24) & 0xFF));
    }

    json sample;
    sample["time"] = format_timestamp(s.time_fs);
    sample["item_values"] = base64_encode(raw.data(), raw.size());
    return sample;
}

class VcsVpiBackend {
public:
    void initialize() {
        discover_signals();
    }

    uint64_t time_fs() const {
        s_vpi_time t;
        std::memset(&t, 0, sizeof(t));
        t.type = vpiSimTime;
        vpi_get_time(nullptr, &t);
        uint64_t ticks = ((uint64_t)t.high << 32) | (uint32_t)t.low;
        return ticks_to_fs(ticks);
    }

    const std::map<std::string, SignalInfo>& signals() const {
        return signals_by_display_;
    }

    SignalInfo* resolve_signal(const std::string& requested) {
        if (!contains_dot_or_space(requested)) {
            const std::string root = top_vpi_scope();
            std::vector<std::string> top_ports = {
                root + "." + requested,
            };
            for (const auto& top_port : top_ports) {
                vpiHandle handle =
                    vpi_handle_by_name((PLI_BYTE8*)top_port.c_str(), nullptr);
                if (handle != nullptr) {
                    std::string display = register_signal(handle);
                    auto it = signals_by_display_.find(display);
                    if (it != signals_by_display_.end()) {
                        return &it->second;
                    }
                }
            }
        }

        auto alias = aliases_.find(requested);
        if (alias != aliases_.end()) {
            auto it = signals_by_display_.find(alias->second);
            if (it != signals_by_display_.end()) {
                return &it->second;
            }
        }

        for (const auto& candidate : candidate_vpi_names(requested)) {
            vpiHandle handle =
                vpi_handle_by_name((PLI_BYTE8*)candidate.c_str(), nullptr);
            if (handle != nullptr) {
                std::string display = register_signal(handle);
                auto it = signals_by_display_.find(display);
                if (it != signals_by_display_.end()) {
                    return &it->second;
                }
            }
        }
        return nullptr;
    }

    bool read_u32(const std::string& name, uint32_t& value) {
        SignalInfo* signal = resolve_signal(name);
        if (signal == nullptr) {
            return false;
        }
        auto chunks = read_chunks(*signal);
        value = chunks.empty() ? 0 : chunks[0];
        return true;
    }

    std::vector<uint32_t> read_chunks(const SignalInfo& signal) {
        size_t chunks = signal.num_chunks == 0 ? 1 : signal.num_chunks;
        std::vector<uint32_t> values(chunks, 0);

        s_vpi_value value;
        std::memset(&value, 0, sizeof(value));
        value.format = vpiVectorVal;
        vpi_get_value(signal.handle, &value);

        if (value.value.vector != nullptr) {
            for (size_t i = 0; i < chunks; ++i) {
                values[i] = (uint32_t)value.value.vector[i].aval;
            }
            return values;
        }

        std::memset(&value, 0, sizeof(value));
        value.format = vpiIntVal;
        vpi_get_value(signal.handle, &value);
        values[0] = (uint32_t)value.value.integer;
        return values;
    }

    bool write_chunks(const std::string& name,
                      const std::vector<uint32_t>& chunks) {
        SignalInfo* signal = resolve_signal(name);
        if (signal == nullptr || !signal->writable ||
            chunks.size() != signal->num_chunks) {
            std::cerr << "[DIGITAL-REMOTE] Warning: signal '" << name
                      << "' is not a writable port with matching width"
                      << std::endl;
            return false;
        }

        s_vpi_value put_value;
        std::memset(&put_value, 0, sizeof(put_value));
        std::vector<s_vpi_vecval> vector_value(signal->num_chunks);
        for (size_t index = 0; index < signal->num_chunks; ++index) {
            vector_value[index].aval = (PLI_INT32)chunks[index];
            vector_value[index].bval = 0;
        }
        put_value.format = vpiVectorVal;
        put_value.value.vector = vector_value.data();
        vpi_put_value(signal->handle, &put_value, nullptr, vpiForceFlag);
        return true;
    }

private:
    void discover_signals() {
        vpiHandle root_iter = vpi_iterate(vpiModule, nullptr);
        if (root_iter != nullptr) {
            vpiHandle module = nullptr;
            while ((module = vpi_scan(root_iter)) != nullptr) {
                scan_ports(module);
                discover_scope(module);
            }
        }

        vpiHandle root =
            vpi_handle_by_name((PLI_BYTE8*)top_vpi_scope().c_str(), nullptr);
        if (root != nullptr) {
            scan_ports(root);
            discover_scope(root);
        }
    }

    void scan_ports(vpiHandle module) {
        vpiHandle iter = vpi_iterate(vpiPort, module);
        if (iter == nullptr) return;
        vpiHandle port = nullptr;
        while ((port = vpi_scan(iter)) != nullptr) {
            int direction = vpi_get(vpiDirection, port);
            vpiHandle signal = vpi_handle(vpiLowConn, port);
            if (signal == nullptr) signal = vpi_handle(vpiHighConn, port);
            if (signal != nullptr) register_signal(signal, direction);
        }
    }

    void discover_scope(vpiHandle scope) {
        if (scope == nullptr) {
            return;
        }

        // VPI strings may share a scratch buffer. Own the full name before
        // making another vpi_get_str call (ModelSim reuses this buffer).
        const char* full_name_raw = vpi_get_str(vpiFullName, scope);
        std::string scope_name = full_name_raw ? std::string(full_name_raw) : std::string();
        if (scope_name.empty()) {
            const char* name_raw = vpi_get_str(vpiName, scope);
            if (name_raw) scope_name = name_raw;
        }

        scan_signal_kind(scope, vpiNet);
        scan_signal_kind(scope, vpiReg);
        scan_signal_kind(scope, vpiIntegerVar);
#ifdef vpiBitVar
        scan_signal_kind(scope, vpiBitVar);
#endif
#ifdef vpiByteVar
        scan_signal_kind(scope, vpiByteVar);
#endif
#ifdef vpiShortIntVar
        scan_signal_kind(scope, vpiShortIntVar);
#endif
#ifdef vpiIntVar
        scan_signal_kind(scope, vpiIntVar);
#endif
#ifdef vpiLongIntVar
        scan_signal_kind(scope, vpiLongIntVar);
#endif
#ifdef vpiLogicVar
        scan_signal_kind(scope, vpiLogicVar);
#endif

        scan_child_scope_kind(scope, vpiModule);
#ifdef vpiInterface
        scan_child_scope_kind(scope, vpiInterface);
#endif
#ifdef vpiGenScope
        scan_child_scope_kind(scope, vpiGenScope);
#endif
#ifdef vpiNamedBegin
        scan_child_scope_kind(scope, vpiNamedBegin);
#endif
#ifdef vpiNamedFork
        scan_child_scope_kind(scope, vpiNamedFork);
#endif
    }

    void scan_signal_kind(vpiHandle scope, int kind) {
        vpiHandle iter = vpi_iterate(kind, scope);
        if (iter == nullptr) {
            return;
        }

        vpiHandle item = nullptr;
        while ((item = vpi_scan(iter)) != nullptr) {
            register_signal(item);
        }
    }

    void scan_child_scope_kind(vpiHandle scope, int kind) {
        vpiHandle iter = vpi_iterate(kind, scope);
        if (iter == nullptr) {
            return;
        }

        vpiHandle child = nullptr;
        while ((child = vpi_scan(iter)) != nullptr) {
            discover_scope(child);
        }
    }

    std::string register_signal(vpiHandle handle,
                                int direction_override = vpiNoDirection) {
        // VPI strings may share a scratch buffer. Own the full name before
        // making another vpi_get_str call (ModelSim reuses this buffer).
        const char* full_name_raw = vpi_get_str(vpiFullName, handle);
        std::string vpi_name = full_name_raw ? std::string(full_name_raw) : std::string();
        if (vpi_name.empty()) {
            const char* name_raw = vpi_get_str(vpiName, handle);
            if (name_raw) vpi_name = name_raw;
        }

        if (vpi_name.empty()) {
            return std::string();
        }

        auto by_vpi = vpi_to_display_.find(vpi_name);
        if (by_vpi != vpi_to_display_.end()) {
            if (direction_override == vpiInput ||
                direction_override == vpiOutput ||
                direction_override == vpiInout) {
                auto existing = signals_by_display_.find(by_vpi->second);
                if (existing != signals_by_display_.end()) {
                    existing->second.direction = direction_override;
                    existing->second.writable =
                        direction_override == vpiInput ||
                        direction_override == vpiInout;
                }
            }
            return by_vpi->second;
        }

        int width_value = vpi_get(vpiSize, handle);
        uint32_t width = width_value > 0 ? (uint32_t)width_value : 1;
        SignalInfo signal;
        signal.display_name = display_name_from_vpi(vpi_name);
        signal.vpi_name = vpi_name;
        signal.handle = handle;
        signal.width = width;
        signal.num_chunks = (width + 31) / 32;
        if (signal.num_chunks == 0) {
            signal.num_chunks = 1;
        }
        int detected_direction = direction_override;
        if (detected_direction != vpiInput && detected_direction != vpiOutput &&
            detected_direction != vpiInout) {
            detected_direction = vpi_get(vpiDirection, handle);
        }
        signal.direction = detected_direction;
        signal.writable = detected_direction == vpiInput ||
                          detected_direction == vpiInout;

        auto inserted = signals_by_display_.emplace(signal.display_name, signal);
        if (!inserted.second) {
            inserted.first->second = signal;
        }
        vpi_to_display_[vpi_name] = signal.display_name;
        register_aliases(signal);
        return signal.display_name;
    }

    void register_aliases(const SignalInfo& signal) {
        put_alias(signal.display_name, signal.display_name);
        put_alias(signal.vpi_name, signal.display_name);
        put_alias(replace_char(signal.vpi_name, '.', ' '), signal.display_name);
        auto last_dot = signal.vpi_name.rfind('.');
        if (last_dot != std::string::npos && last_dot + 1 < signal.vpi_name.size()) {
            put_alias(signal.vpi_name.substr(last_dot + 1), signal.display_name);
        }

        const std::string root = top_vpi_scope();
        const std::string display_root = top_display_scope();
        const std::string root_dot = root + ".";
        const std::string display_root_space = display_root + " ";

        if (starts_with(signal.vpi_name, root_dot)) {
            std::string rest_dot = signal.vpi_name.substr(root_dot.size());
            std::string rest_space = replace_char(rest_dot, '.', ' ');
            put_alias(display_root_space + rest_space, signal.display_name);
            put_alias(rest_dot, signal.display_name);
            put_alias(rest_space, signal.display_name);
        }
    }

    void put_alias(const std::string& alias, const std::string& display) {
        if (!alias.empty() && aliases_.find(alias) == aliases_.end()) {
            aliases_[alias] = display;
        }
    }

    std::vector<std::string> candidate_vpi_names(const std::string& requested) const {
        std::vector<std::string> names;
        auto add = [&names](const std::string& value) {
            if (!value.empty() &&
                std::find(names.begin(), names.end(), value) == names.end()) {
                names.push_back(value);
            }
        };

        const std::string root = top_vpi_scope();
        const std::string display_root = top_display_scope();
        const std::string dotted = replace_char(requested, ' ', '.');

        add(requested);
        add(dotted);
        add(root + "." + requested);
        add(root + "." + dotted);

        const std::string display_space = display_root + " ";
        const std::string display_dot = display_root + ".";
        if (starts_with(requested, display_space)) {
            std::string rest = requested.substr(display_space.size());
            add(root + "." + replace_char(rest, ' ', '.'));
        }
        if (starts_with(dotted, display_dot)) {
            std::string rest = dotted.substr(display_dot.size());
            add(root + "." + rest);
        }

        return names;
    }

    std::string display_name_from_vpi(const std::string& vpi_name) const {
        const std::string root = top_vpi_scope();
        const std::string display_root = top_display_scope();
        const std::string root_dot = root + ".";

        if (vpi_name == root) {
            return display_root;
        }
        if (starts_with(vpi_name, root_dot)) {
            std::string rest = vpi_name.substr(root_dot.size());
            return display_root + " " + replace_char(rest, '.', ' ');
        }
        return display_root + " " + replace_char(vpi_name, '.', ' ');
    }

    std::string top_vpi_scope() const {
        return XSTR(VCS_REMOTE_TOP_SCOPE);
    }

    std::string top_display_scope() const {
        return XSTR(VCS_REMOTE_DISPLAY_SCOPE);
    }

    std::map<std::string, SignalInfo> signals_by_display_;
    std::map<std::string, std::string> vpi_to_display_;
    std::map<std::string, std::string> aliases_;
    std::set<std::string> visited_scopes_;
};

static const char* direction_name(int direction) {
    if (direction == vpiInput) return "input";
    if (direction == vpiOutput) return "output";
    if (direction == vpiInout) return "inout";
    return "internal";
}

static bool parse_stimulus_request(
    const json& request, uint64_t sim_time_fs, bool running,
    VcsVpiBackend& backend,
    const std::set<std::pair<std::string, uint64_t>>& scheduled,
    uint64_t& next_sequence, std::vector<StimulusEvent>& parsed,
    std::string& error_code, std::string& error_message) {
    if (!request.contains("events") || !request["events"].is_array()) {
        error_code = "INVALID_VALUE";
        error_message = "events must be an array";
        return false;
    }
    const std::string command = request.value("command", "");
    std::set<std::pair<std::string, uint64_t>> batch_keys;
    try {
        for (const auto& value : request["events"]) {
            StimulusEvent event;
            event.time_fs = parse_timestamp(value.at("time").get<std::string>());
            event.port = value.at("port").get<std::string>();
            SignalInfo* signal = backend.resolve_signal(event.port);
            if (signal == nullptr || !signal->writable) {
                error_code = "PORT_NOT_WRITABLE";
                error_message = "port is not a writable input: " + event.port;
                return false;
            }
            event.width = signal->width;
            if ((running && event.time_fs <= sim_time_fs) ||
                (!running && event.time_fs < sim_time_fs)) {
                error_code = "LATE_STIMULUS";
                error_message = "stimulus timestamp is not ahead of simulator time";
                return false;
            }
            auto key = std::make_pair(event.port, event.time_fs);
            if (!batch_keys.insert(key).second || scheduled.count(key) != 0) {
                error_code = "DUPLICATE_EVENT";
                error_message = "duplicate event for port and timestamp";
                return false;
            }
            if (command == "inject_stimulus_v2") {
                uint32_t width = value.at("width").get<uint32_t>();
                if (width == 0 || width != event.width) {
                    error_code = "WIDTH_MISMATCH";
                    error_message = "stimulus width does not match port width";
                    return false;
                }
                std::vector<uint8_t> bytes;
                if (!base64_decode(value.at("value_chunks_base64").get<std::string>(),
                                   bytes) ||
                    bytes.size() != signal->num_chunks * 4) {
                    error_code = "INVALID_VALUE";
                    error_message = "invalid little-endian u32 chunk payload";
                    return false;
                }
                event.chunks.resize(signal->num_chunks);
                for (size_t index = 0; index < event.chunks.size(); ++index) {
                    size_t base = index * 4;
                    event.chunks[index] = (uint32_t)bytes[base] |
                        ((uint32_t)bytes[base + 1] << 8) |
                        ((uint32_t)bytes[base + 2] << 16) |
                        ((uint32_t)bytes[base + 3] << 24);
                }
                if (event.width % 32 != 0 &&
                    (event.chunks.back() >> (event.width % 32)) != 0) {
                    error_code = "INVALID_VALUE";
                    error_message = "unused high stimulus bits must be zero";
                    return false;
                }
            } else {
                if (event.width > 32) {
                    error_code = "WIDTH_MISMATCH";
                    error_message = "legacy stimulus cannot drive ports wider than 32 bits";
                    return false;
                }
                uint32_t word = value.at("value").get<uint32_t>();
                if (event.width < 32 && (word >> event.width) != 0) {
                    error_code = "INVALID_VALUE";
                    error_message = "stimulus value exceeds port width";
                    return false;
                }
                event.chunks = {word};
            }
            event.sequence = next_sequence + parsed.size();
            parsed.push_back(std::move(event));
        }
    } catch (const std::exception& error) {
        error_code = "INVALID_VALUE";
        error_message = error.what();
        return false;
    }
    next_sequence += parsed.size();
    return true;
}

static void enqueue_stimulus(
    const std::vector<StimulusEvent>& events,
    std::priority_queue<StimulusEvent, std::vector<StimulusEvent>,
                        StimulusCompare>& queue,
    std::set<std::pair<std::string, uint64_t>>& scheduled) {
    for (const auto& event : events) {
        queue.push(event);
        scheduled.insert({event.port, event.time_fs});
    }
}

static VcsVpiBackend g_backend;

static bool evaluate_stop_conditions(const std::vector<json>& conditions,
                                     VcsVpiBackend& backend,
                                     json& matched_detail) {
    for (const auto& cond : conditions) {
        std::string type = cond.value("type", "");

        if (type == "signal_value") {
            std::string signal = cond.value("signal", "");
            uint32_t current = 0;
            uint32_t target = cond.value("value", (uint32_t)0);
            if (backend.read_u32(signal, current) && current == target) {
                matched_detail = {
                    {"type", "signal_value"},
                    {"signal", signal},
                    {"matched_value", current},
                };
                return true;
            }
        } else if (type == "signal_diff") {
            std::string sig_a = cond.value("signal_a", "");
            std::string sig_b = cond.value("signal_b", "");
            uint32_t val_a = 0;
            uint32_t val_b = 0;
            if (backend.read_u32(sig_a, val_a) &&
                backend.read_u32(sig_b, val_b) &&
                val_a != val_b) {
                matched_detail = {
                    {"type", "signal_diff"},
                    {"signal_a", sig_a},
                    {"signal_b", sig_b},
                    {"value_a", val_a},
                    {"value_b", val_b},
                };
                return true;
            }
        } else if (type == "signal_pattern") {
            std::string signal = cond.value("signal", "");
            uint32_t current = 0;
            uint32_t mask = cond.value("mask", (uint32_t)0xFFFFFFFF);
            uint32_t target = cond.value("value", (uint32_t)0);
            if (backend.read_u32(signal, current) &&
                ((current & mask) == target)) {
                matched_detail = {
                    {"type", "signal_pattern"},
                    {"signal", signal},
                    {"matched_value", current},
                    {"mask", mask},
                };
                return true;
            }
        }
    }
    return false;
}

struct VcsRunState {
    bool active = false;
    bool pause_requested = false;
    bool readonly_sample_pending = false;
    bool run_boundary_pending = false;
    uint64_t run_id = 0;
    uint64_t until_time_fs = 0;
    bool sample_item_values = false;
    bool streaming = false;
    uint32_t batch_size = 64;
    uint64_t batch_interval_fs = 10000000000ULL;
    uint64_t max_samples = 1000000;
    uint64_t total_samples = 0;
    uint64_t last_batch_time_fs = 0;
    std::string pause_cause = "until_time";
    json stop_detail;
    std::vector<json> stop_conditions;
    std::vector<TrackedSignal>* tracked = nullptr;
    std::vector<Sample> pending_batch;
    std::vector<vpiHandle> value_change_callbacks;
};

struct VcsCallbackServer {
    std::map<std::string, std::vector<TrackedSignal>> references;
    std::vector<Sample> recorded_samples;
    std::priority_queue<StimulusEvent,
                        std::vector<StimulusEvent>,
                        StimulusCompare>
        stimulus_queue;
    std::set<std::pair<std::string, uint64_t>> scheduled_stimulus;
    uint64_t next_stimulus_sequence = 0;
    std::mutex incoming_mutex;
    std::condition_variable incoming_cv;
    std::deque<std::string> incoming;
    bool connection_alive = true;
    VcsRunState run;
};

static VcsCallbackServer g_callback_server;

static void socket_reader_loop(socket_t client) {
    std::vector<char> read_buf;
    std::string message;
    while (!g_server_stop.load() && recv_message(client, message, read_buf)) {
        {
            std::lock_guard<std::mutex> lock(g_callback_server.incoming_mutex);
            g_callback_server.incoming.push_back(message);
        }
        g_callback_server.incoming_cv.notify_one();
    }
    {
        std::lock_guard<std::mutex> lock(g_callback_server.incoming_mutex);
        g_callback_server.connection_alive = false;
    }
    g_callback_server.incoming_cv.notify_one();
}

static bool pop_incoming(std::string& message, bool wait) {
    std::unique_lock<std::mutex> lock(g_callback_server.incoming_mutex);
    if (wait) {
        g_callback_server.incoming_cv.wait(lock, []() {
            return !g_callback_server.incoming.empty() ||
                   !g_callback_server.connection_alive ||
                   g_server_stop.load();
        });
    }
    if (g_callback_server.incoming.empty()) {
        return false;
    }
    message = g_callback_server.incoming.front();
    g_callback_server.incoming.pop_front();
    return true;
}

static bool callback_connection_alive() {
    std::lock_guard<std::mutex> lock(g_callback_server.incoming_mutex);
    return g_callback_server.connection_alive;
}

static bool parse_request(const std::string& message, json& request) {
    request = json::parse(message, nullptr, false);
    if (request.is_discarded()) {
        send_error(g_client, json::object(), "INVALID_JSON", "JSON parse error");
        return false;
    }
    return true;
}

static bool validate_command(const json& request) {
    if (request.value("type", "") != "command") {
        send_error(g_client, request, "UNKNOWN_MESSAGE_TYPE",
                   "Unknown message type: " + request.value("type", ""));
        return false;
    }
    if (!request.contains("request_id") ||
        !request["request_id"].is_string() ||
        request["request_id"].get<std::string>().empty()) {
        send_error(g_client, request, "MISSING_REQUEST_ID",
                   "protocol v1 command requires request_id");
        return false;
    }
    return true;
}

static void send_greeting() {
    json greeting;
    greeting["type"] = "greeting";
    greeting["version"] = 0;
    greeting["protocol_version"] = 1;
    greeting["commands"] = json::array({
        "list_scopes", "list_items", "get_simulation_status",
        "query_interval", "reference_items", "run_simulation",
        "pause_simulation"
    });
#if DIDE_REMOTE_LIVE_STIMULUS
    greeting["commands"].push_back("inject_stimulus");
    greeting["commands"].push_back("inject_stimulus_v2");
#endif
    greeting["events"] = json::array({
        "simulation_paused", "simulation_finished", "sample_batch"
    });
    greeting["features"] = {
        {"live_stimulus", DIDE_REMOTE_LIVE_STIMULUS != 0},
        {"wide_stimulus", DIDE_REMOTE_LIVE_STIMULUS != 0},
        {"streaming", true},
        {"pause_barrier", true},
    };
    send_message(g_client, greeting.dump());
}

static void callback_list_scopes(const json& request) {
    std::map<std::string, json> scopes;
    for (const auto& entry : g_backend.signals()) {
        const std::string& name = entry.first;
        auto last_space = name.rfind(' ');
        if (last_space != std::string::npos) {
            scopes[name.substr(0, last_space)] = json::object();
        }
    }
    json response;
    response["type"] = "response";
    response["command"] = "list_scopes";
    response["scopes"] = json::object();
    for (const auto& entry : scopes) {
        response["scopes"][entry.first] = entry.second;
    }
    send_response(g_client, request, response);
}

static void callback_list_items(const json& request) {
    std::string scope_filter;
    if (request.contains("scope") && !request["scope"].is_null()) {
        scope_filter = request["scope"].get<std::string>();
    }
    json items = json::object();
    for (const auto& entry : g_backend.signals()) {
        if (!scope_filter.empty() &&
            entry.first.substr(0, scope_filter.size()) != scope_filter) {
            continue;
        }
        json item;
        item["width"] = entry.second.width;
        item["direction"] = direction_name(entry.second.direction);
        item["writable"] = entry.second.writable;
        items[entry.first] = item;
    }
    json response;
    response["type"] = "response";
    response["command"] = "list_items";
    response["items"] = items;
    send_response(g_client, request, response);
}

static void callback_reference_items(const json& request) {
    std::string reference = request.value("reference", "DEFAULT");
    auto& tracked = g_callback_server.references[reference];
    tracked.clear();
    if (request.contains("items") && request["items"].is_array()) {
        for (const auto& item_array : request["items"]) {
            if (!item_array.is_array() || item_array.empty()) {
                continue;
            }
            std::string signal_name = item_array[0].get<std::string>();
            SignalInfo* signal = g_backend.resolve_signal(signal_name);
            if (signal == nullptr) {
                std::cerr << "[DIGITAL-REMOTE] Warning: signal '"
                          << signal_name << "' not found" << std::endl;
                continue;
            }
            TrackedSignal tracked_signal;
            tracked_signal.name = signal_name;
            tracked_signal.signal = signal;
            tracked.push_back(tracked_signal);
        }
    }
    json response;
    response["type"] = "response";
    response["command"] = "reference_items";
    send_response(g_client, request, response);
}

static void callback_inject(const json& request, bool running) {
#if !DIDE_REMOTE_LIVE_STIMULUS
    (void)running;
    send_error(g_client, request, "UNSUPPORTED_COMMAND",
               "this simulator backend does not support external stimulus injection");
    return;
#else
    std::vector<StimulusEvent> parsed;
    std::string code;
    std::string message;
    if (!parse_stimulus_request(
            request, g_backend.time_fs(), running, g_backend,
            g_callback_server.scheduled_stimulus,
            g_callback_server.next_stimulus_sequence,
            parsed, code, message)) {
        send_error(g_client, request, code, message);
        return;
    }
    enqueue_stimulus(parsed, g_callback_server.stimulus_queue,
                     g_callback_server.scheduled_stimulus);
    json response = {
        {"type", "response"},
        {"command", request.value("command", "")},
        {"count", parsed.size()},
    };
    send_response(g_client, request, response);
#endif
}

static void callback_query_interval(const json& request) {
    uint64_t start_fs = 0;
    uint64_t end_fs = std::numeric_limits<uint64_t>::max();
    if (request.contains("interval") && request["interval"].is_array() &&
        request["interval"].size() == 2) {
        start_fs = parse_timestamp(request["interval"][0].get<std::string>());
        end_fs = parse_timestamp(request["interval"][1].get<std::string>());
    }
    json samples = json::array();
    for (const auto& sample : g_callback_server.recorded_samples) {
        if (sample.time_fs >= start_fs && sample.time_fs <= end_fs) {
            samples.push_back(encode_sample_json(sample));
        }
    }
    json response;
    response["type"] = "response";
    response["command"] = "query_interval";
    response["samples"] = samples;
    send_response(g_client, request, response);
}

static PLI_INT32 run_step_cb(p_cb_data);
static PLI_INT32 sample_step_cb(p_cb_data);
static PLI_INT32 value_change_cb(p_cb_data);

static void schedule_callback(int reason, PLI_INT32 (*callback)(p_cb_data),
                              uint64_t delay_ticks) {
    s_vpi_time time;
    std::memset(&time, 0, sizeof(time));
    time.type = vpiSimTime;
    time.high = (PLI_UINT32)(delay_ticks >> 32);
    time.low = (PLI_UINT32)delay_ticks;
    s_cb_data callback_data;
    std::memset(&callback_data, 0, sizeof(callback_data));
    callback_data.reason = reason;
    callback_data.cb_rtn = callback;
    callback_data.time = &time;
    vpi_register_cb(&callback_data);
}

static void schedule_run_step(uint64_t target_time_fs) {
    uint64_t current_time_fs = g_backend.time_fs();
    uint64_t delay_fs =
        target_time_fs > current_time_fs ? target_time_fs - current_time_fs : 0;
    uint64_t delay_ticks = delay_fs / simulator_tick_fs();
    if (delay_ticks == 0) {
        delay_ticks = 1;
    }
    schedule_callback(cbAfterDelay, run_step_cb, delay_ticks);
}

static void remove_value_change_callbacks() {
    VcsRunState& run = g_callback_server.run;
    for (vpiHandle callback : run.value_change_callbacks) {
        if (callback != nullptr) {
            vpi_remove_cb(callback);
        }
    }
    run.value_change_callbacks.clear();
}

static bool install_value_change_callbacks() {
    VcsRunState& run = g_callback_server.run;
    remove_value_change_callbacks();
    if (!run.sample_item_values || run.tracked == nullptr) {
        return true;
    }

    std::set<vpiHandle> registered_signals;
    for (const auto& tracked : *run.tracked) {
        if (tracked.signal == nullptr || tracked.signal->handle == nullptr ||
            !registered_signals.insert(tracked.signal->handle).second) {
            continue;
        }

        s_vpi_time time;
        std::memset(&time, 0, sizeof(time));
        time.type = vpiSimTime;
        s_vpi_value value;
        std::memset(&value, 0, sizeof(value));
        value.format = vpiSuppressVal;
        s_cb_data callback_data;
        std::memset(&callback_data, 0, sizeof(callback_data));
        callback_data.reason = cbValueChange;
        callback_data.cb_rtn = value_change_cb;
        callback_data.obj = tracked.signal->handle;
        callback_data.time = &time;
        callback_data.value = &value;
        vpiHandle callback = vpi_register_cb(&callback_data);
        if (callback == nullptr) {
            remove_value_change_callbacks();
            return false;
        }
        run.value_change_callbacks.push_back(callback);
    }
    return true;
}

static void request_readonly_sample(bool run_boundary) {
    VcsRunState& run = g_callback_server.run;
    if (!run.active || g_server_stop.load()) {
        return;
    }
    run.run_boundary_pending = run.run_boundary_pending || run_boundary;
    if (run.readonly_sample_pending) {
        return;
    }
    run.readonly_sample_pending = true;
    schedule_callback(cbReadOnlySynch, sample_step_cb, 0);
}

static bool start_run(const json& request) {
    if (!request.contains("run_id") ||
        !request["run_id"].is_number_unsigned()) {
        send_error(g_client, request, "INVALID_RUN_ID",
                   "run_simulation requires an unsigned run_id");
        return false;
    }
    VcsRunState run;
    run.active = true;
    run.run_id = request["run_id"].get<uint64_t>();
    run.until_time_fs = g_backend.time_fs();
    if (request.contains("until_time") && !request["until_time"].is_null()) {
        run.until_time_fs =
            parse_timestamp(request["until_time"].get<std::string>());
    }
    if (run.until_time_fs < g_backend.time_fs()) {
        send_error(g_client, request, "LATE_STIMULUS",
                   "run target is earlier than simulator time");
        return false;
    }
    run.sample_item_values = request.value("sample_item_values", false);
    run.streaming = request.value("streaming", false);
    run.batch_size = request.value("batch_size", (uint32_t)64);
    run.batch_interval_fs =
        request.value("batch_interval_fs", (uint64_t)10000000000ULL);
    run.max_samples = request.value("max_samples", (uint64_t)1000000);
    if (request.contains("stop_conditions") &&
        request["stop_conditions"].is_array()) {
        for (const auto& condition : request["stop_conditions"]) {
            run.stop_conditions.push_back(condition);
        }
    }
    if (request.contains("items") && !request["items"].is_null()) {
        auto requested =
            g_callback_server.references.find(request["items"].get<std::string>());
        if (requested != g_callback_server.references.end() &&
            !requested->second.empty()) {
            run.tracked = &requested->second;
        }
    }
    if (run.tracked == nullptr) {
        for (auto& entry : g_callback_server.references) {
            if (!entry.second.empty()) {
                run.tracked = &entry.second;
                break;
            }
        }
    }
    run.last_batch_time_fs = g_backend.time_fs();
    g_callback_server.recorded_samples.clear();
    g_callback_server.run = run;
    if (!install_value_change_callbacks()) {
        g_callback_server.run.active = false;
        send_error(g_client, request, "VPI_CALLBACK_REGISTRATION_FAILED",
                   "failed to register a value-change callback for a tracked signal");
        return false;
    }
    g_active_run_id.store(run.run_id);
    json response;
    response["type"] = "response";
    response["command"] = "run_simulation";
    send_response(g_client, request, response);
    schedule_run_step(g_backend.time_fs());
    return true;
}

static bool paused_command_loop() {
    while (!g_server_stop.load() && callback_connection_alive()) {
        std::string message;
        if (!pop_incoming(message, true)) {
            continue;
        }
        json request;
        if (!parse_request(message, request)) {
            continue;
        }
        if (request.value("type", "") == "greeting") {
            send_greeting();
            continue;
        }
        if (!validate_command(request)) {
            continue;
        }
        std::string command = request.value("command", "");
        if (command == "list_scopes") {
            callback_list_scopes(request);
        } else if (command == "list_items") {
            callback_list_items(request);
        } else if (command == "get_simulation_status") {
            json response = {
                {"type", "response"},
                {"command", "get_simulation_status"},
                {"status", "paused"},
                {"latest_time", format_timestamp(g_backend.time_fs())},
            };
            send_response(g_client, request, response);
        } else if (command == "reference_items") {
            callback_reference_items(request);
        } else if (command == "inject_stimulus" ||
                   command == "inject_stimulus_v2") {
            callback_inject(request, false);
        } else if (command == "run_simulation") {
            if (start_run(request)) {
                return true;
            }
        } else if (command == "query_interval") {
            callback_query_interval(request);
        } else if (command == "pause_simulation") {
            json response = {
                {"type", "response"},
                {"command", "pause_simulation"},
                {"time", format_timestamp(g_backend.time_fs())},
            };
            send_response(g_client, request, response);
        } else {
            send_error(g_client, request, "UNKNOWN_COMMAND",
                       "Unknown command: " + command);
        }
    }
    std::cerr << "[DIGITAL-REMOTE] Client EOF while simulation was paused"
              << std::endl;
    vpi_control(vpiFinish, 0);
    return false;
}

static void process_running_commands() {
    std::string message;
    while (pop_incoming(message, false)) {
        json request;
        if (!parse_request(message, request) || !validate_command(request)) {
            continue;
        }
        if (!request.contains("run_id") ||
            !request["run_id"].is_number_unsigned() ||
            request["run_id"].get<uint64_t>() != g_callback_server.run.run_id) {
            send_error(g_client, request, "RUN_ID_MISMATCH",
                       "command does not target the active run_id");
            continue;
        }
        std::string command = request.value("command", "");
        if (command == "pause_simulation") {
            json response = {
                {"type", "response"},
                {"command", "pause_simulation"},
                {"time", format_timestamp(g_backend.time_fs())},
            };
            send_response(g_client, request, response);
            g_callback_server.run.pause_requested = true;
            g_callback_server.run.pause_cause = "client_pause";
        } else if (command == "inject_stimulus" ||
                   command == "inject_stimulus_v2") {
            callback_inject(request, true);
        } else {
            send_error(g_client, request, "UNKNOWN_COMMAND",
                       "unsupported command while simulation is running: " +
                           command);
        }
    }
}

static bool callback_record_sample() {
    VcsRunState& run = g_callback_server.run;
    if (!run.sample_item_values || run.tracked == nullptr) {
        return false;
    }
    Sample sample;
    sample.time_fs = g_backend.time_fs();
    for (const auto& tracked : *run.tracked) {
        if (tracked.signal == nullptr) {
            continue;
        }
        auto chunks = g_backend.read_chunks(*tracked.signal);
        sample.item_values.insert(sample.item_values.end(),
                                  chunks.begin(), chunks.end());
    }
    if (!g_callback_server.recorded_samples.empty() &&
        g_callback_server.recorded_samples.back().item_values ==
            sample.item_values) {
        return false;
    }
    g_callback_server.recorded_samples.push_back(sample);
    ++run.total_samples;
    return true;
}

static void flush_callback_samples() {
    VcsRunState& run = g_callback_server.run;
    if (!callback_connection_alive() ||
        !run.streaming || run.pending_batch.empty()) {
        return;
    }
    json event;
    event["type"] = "event";
    event["run_id"] = run.run_id;
    event["event"] = "sample_batch";
    event["samples"] = json::array();
    for (const auto& sample : run.pending_batch) {
        event["samples"].push_back(encode_sample_json(sample));
    }
    send_message(g_client, event.dump());
    run.pending_batch.clear();
    run.last_batch_time_fs = g_backend.time_fs();
}

static void pause_callback_run() {
    VcsRunState& run = g_callback_server.run;
    remove_value_change_callbacks();
    run.readonly_sample_pending = false;
    run.run_boundary_pending = false;
    flush_callback_samples();
    json event;
    event["type"] = "event";
    event["run_id"] = run.run_id;
    event["event"] = "simulation_paused";
    event["time"] = format_timestamp(g_backend.time_fs());
    event["cause"] = run.pause_cause;
    if (!run.stop_detail.is_null()) {
        event["stop_detail"] = run.stop_detail;
    }
    send_message(g_client, event.dump());
    run.active = false;
    g_active_run_id.store(0);
    paused_command_loop();
}

static PLI_INT32 run_step_cb(p_cb_data) {
    if (!g_callback_server.run.active || g_server_stop.load()) {
        return 0;
    }
    process_running_commands();
    if (!callback_connection_alive()) {
        std::cerr << "[DIGITAL-REMOTE] Client EOF while simulation was running"
                  << std::endl;
        vpi_control(vpiFinish, 0);
        return 0;
    }
    uint64_t current_time_fs = g_backend.time_fs();
    while (!g_callback_server.stimulus_queue.empty() &&
           g_callback_server.stimulus_queue.top().time_fs <= current_time_fs) {
        StimulusEvent event = g_callback_server.stimulus_queue.top();
        g_callback_server.stimulus_queue.pop();
        g_callback_server.scheduled_stimulus.erase(
            {event.port, event.time_fs});
        g_backend.write_chunks(event.port, event.chunks);
    }
    request_readonly_sample(true);
    return 0;
}

static PLI_INT32 value_change_cb(p_cb_data) {
    request_readonly_sample(false);
    return 0;
}

static PLI_INT32 sample_step_cb(p_cb_data) {
    VcsRunState& run = g_callback_server.run;
    if (!run.active || g_server_stop.load()) {
        return 0;
    }
    bool run_boundary = run.run_boundary_pending;
    run.readonly_sample_pending = false;
    run.run_boundary_pending = false;
    uint64_t current_time_fs = g_backend.time_fs();
    bool new_sample = callback_record_sample();
    if (run.streaming && new_sample) {
        run.pending_batch.push_back(g_callback_server.recorded_samples.back());
        bool time_trigger =
            current_time_fs - run.last_batch_time_fs >= run.batch_interval_fs;
        bool size_trigger =
            run.pending_batch.size() >= (size_t)std::max(run.batch_size, 1U);
        if (time_trigger || size_trigger) {
            flush_callback_samples();
        }
    }
    if (!run.stop_conditions.empty() &&
        evaluate_stop_conditions(run.stop_conditions, g_backend,
                                 run.stop_detail)) {
        run.pause_requested = true;
        run.pause_cause = "stop_condition";
    }
    if (run.total_samples >= run.max_samples) {
        run.pause_requested = true;
        run.pause_cause = "max_samples";
    }
    if (current_time_fs >= run.until_time_fs) {
        run.pause_requested = true;
        run.pause_cause = "until_time";
    }
    if (run.pause_requested) {
        pause_callback_run();
        return 0;
    }

    if (!run_boundary) {
        return 0;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(50));
    uint64_t next_time_fs = run.until_time_fs;
    if (!g_callback_server.stimulus_queue.empty()) {
        next_time_fs =
            std::min(next_time_fs,
                     g_callback_server.stimulus_queue.top().time_fs);
    }
    if (next_time_fs <= current_time_fs) {
        next_time_fs = current_time_fs + simulator_tick_fs();
    }
    schedule_run_step(next_time_fs);
    return 0;
}

static PLI_INT32 start_of_simulation_cb(p_cb_data) {
    std::cerr << "[DIGITAL-REMOTE] Starting VPI remote server" << std::endl;

    g_backend.initialize();

    std::cerr << "[DIGITAL-REMOTE] VPI items (" << g_backend.signals().size()
              << "):" << std::endl;
    for (const auto& entry : g_backend.signals()) {
        std::cerr << "  \"" << entry.first << "\" vpi=\""
                  << entry.second.vpi_name << "\" width="
                  << entry.second.width << std::endl;
    }

    int port = 0;
    socket_t listen_sock = open_listener(port);
    if (listen_sock == INVALID_SOCK) {
        std::cerr << "[DIGITAL-REMOTE] Failed to create listening socket" << std::endl;
        return 0;
    }

    // ModelSim/Questa may keep the VPI application's native stderr inside
    // vsimk instead of forwarding it through the launcher process.  Publish
    // the discovery marker through the simulator's VPI output channel as
    // well, which is the portable stream observed by the host launcher.
    vpi_printf((PLI_BYTE8*)"PORT:%d\n", port);
    vpi_flush();
    std::cerr << "PORT:" << port << std::endl;
    std::cerr.flush();
    publish_port_file(port);

    g_client = accept(listen_sock, nullptr, nullptr);
    close_socket(listen_sock);
    if (g_client == INVALID_SOCK) {
        std::cerr << "[DIGITAL-REMOTE] accept failed" << std::endl;
        return 0;
    }

    std::cerr << "[DIGITAL-REMOTE] Client connected" << std::endl;
    g_server_stop.store(false);
    g_server_thread = std::thread(socket_reader_loop, g_client);
    g_server_thread.detach();
    paused_command_loop();
    return 0;
}

static PLI_INT32 end_of_simulation_cb(p_cb_data) {
    VcsRunState& run = g_callback_server.run;
    remove_value_change_callbacks();
    run.readonly_sample_pending = false;
    run.run_boundary_pending = false;
    if (run.active && callback_connection_alive()) {
        flush_callback_samples();
        json event;
        event["type"] = "event";
        event["run_id"] = run.run_id;
        event["event"] = "simulation_finished";
        event["time"] = format_timestamp(g_backend.time_fs());
        send_message(g_client, event.dump());
        run.active = false;
        g_active_run_id.store(0);
    }
    g_server_stop.store(true);
    if (g_client != INVALID_SOCK) {
        close_socket(g_client);
        g_client = INVALID_SOCK;
    }
    g_callback_server.incoming_cv.notify_all();
    return 0;
}

extern "C" void register_vcs_remote_callbacks() {
    s_cb_data cb;
    std::memset(&cb, 0, sizeof(cb));
    cb.reason = cbStartOfSimulation;
    cb.cb_rtn = start_of_simulation_cb;
    vpi_register_cb(&cb);

    std::memset(&cb, 0, sizeof(cb));
    cb.reason = cbEndOfSimulation;
    cb.cb_rtn = end_of_simulation_cb;
    vpi_register_cb(&cb);
}

// Icarus Verilog and ModelSim/Questa discover VPI applications through the
// standard startup table. VCS continues to use the explicit -load entry point
// above, so the same protocol bridge can be compiled for every VPI backend.
extern "C" {
void (*vlog_startup_routines[])() = {
    register_vcs_remote_callbacks,
    nullptr,
};
}
