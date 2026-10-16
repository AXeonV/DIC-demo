// Generic Verilator TCP Simulation Server
//
// Implements the same null-byte-delimited JSON protocol used by
// tests/cxxrtl/cxxrtl_sim_server.cpp, but reads and drives the design through
// Verilator VPI instead of CXXRTL debug_items.
//
// Verilator build shape:
//   verilator --cc --exe --build --vpi --public-flat-rw \
//       --top-module top -CFLAGS -Itests/cxxrtl \
//       -CFLAGS -DTOP_CLASS=Vtop -CFLAGS -DTOP_INCLUDE=Vtop.h \
//       tests/verilator/verilator_sim_server.cpp top.sv
//
// The executable prints PORT:<port> to stderr, then waits for a single client.

#include "verilated.h"
#include "verilated_vpi.h"
#include "vpi_user.h"

#define XSTR_(x) #x
#define XSTR(x) XSTR_(x)

#ifndef TOP_INCLUDE
#error "Define TOP_INCLUDE to the generated Verilator top header, e.g. Vtop.h"
#endif
#include XSTR(TOP_INCLUDE)

#ifndef TOP_CLASS
#error "Define TOP_CLASS to the generated Verilator top class, e.g. Vtop"
#endif

#ifndef TOP_VPI_SCOPE
#define TOP_VPI_SCOPE TOP
#endif

#ifndef TOP_DISPLAY_SCOPE
#define TOP_DISPLAY_SCOPE top
#endif

#ifndef VERILATOR_SERVER_TIME_UNIT_FS
#define VERILATOR_SERVER_TIME_UNIT_FS 0ULL
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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;

double sc_time_stamp() {
    return 0.0;
}

static const uint64_t FEMTOS_PER_SECOND = 1000000000000000ULL;
static const uint64_t FEMTOS_PER_NS = 1000000ULL;

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

static uint64_t context_tick_unit_fs(VerilatedContext& context) {
    const uint64_t configured = VERILATOR_SERVER_TIME_UNIT_FS;
    if (configured != 0) {
        return configured;
    }

    const int precision = context.timeprecision();
    if (precision <= -15) {
        return 1;
    }

    uint64_t unit = 1;
    const int decimal_power = 15 + precision;
    for (int index = 0; index < decimal_power; ++index) {
        if (unit > std::numeric_limits<uint64_t>::max() / 10ULL) {
            return std::numeric_limits<uint64_t>::max();
        }
        unit *= 10ULL;
    }
    return unit;
}

static vluint64_t fs_to_ticks(uint64_t fs, uint64_t unit) {
    return (vluint64_t)(fs / unit);
}

static uint64_t ticks_to_fs(vluint64_t ticks, uint64_t unit) {
    if (ticks > std::numeric_limits<uint64_t>::max() / unit) {
        return std::numeric_limits<uint64_t>::max();
    }
    return (uint64_t)ticks * unit;
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

enum class PollResult { Ready, Empty, Eof, Error };

static PollResult poll_messages(socket_t sock, std::vector<char>& read_buf,
                                std::vector<std::string>& messages) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeval timeout = {0, 0};
    int selected = select((int)sock + 1, &read_set, nullptr, nullptr, &timeout);
    if (selected < 0) return PollResult::Error;
    if (selected == 0) return PollResult::Empty;
    char tmp[4096];
    int count = recv(sock, tmp, sizeof(tmp), 0);
    if (count == 0) return PollResult::Eof;
    if (count < 0) return PollResult::Error;
    read_buf.insert(read_buf.end(), tmp, tmp + count);
    for (;;) {
        auto end = std::find(read_buf.begin(), read_buf.end(), '\0');
        if (end == read_buf.end()) break;
        messages.emplace_back(read_buf.begin(), end);
        read_buf.erase(read_buf.begin(), end + 1);
    }
    return messages.empty() ? PollResult::Empty : PollResult::Ready;
}

struct StimulusEvent {
    uint64_t time_fs;
    std::string port;
    uint32_t width;
    std::vector<uint32_t> chunks;
    uint64_t sequence;
};

struct StimulusCompare {
    bool operator()(const StimulusEvent& a, const StimulusEvent& b) {
        if (a.time_fs != b.time_fs) return a.time_fs > b.time_fs;
        return a.sequence > b.sequence;
    }
};

struct Sample {
    uint64_t time_fs;
    std::vector<uint32_t> item_values;
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

class VerilatorVpiBackend {
public:
    VerilatorVpiBackend(VerilatedContext& context, TOP_CLASS& top)
        : context_(context), top_(top), tick_unit_fs_(context_tick_unit_fs(context)) {}

    void initialize() {
        set_time_fs(0);
        evaluate();
        discover_signals();
    }

    void set_time_fs(uint64_t time_fs) {
        sim_time_fs_ = time_fs;
        context_.time(fs_to_ticks(time_fs, tick_unit_fs_));
        VerilatedVpi::callTimedCbs();
    }

    uint64_t time_fs() const {
        return sim_time_fs_;
    }

    uint64_t next_internal_time_fs() {
        if (!top_.eventsPending()) {
            return std::numeric_limits<uint64_t>::max();
        }
        return ticks_to_fs(top_.nextTimeSlot(), tick_unit_fs_);
    }

    bool finished() {
        return context_.gotFinish();
    }

    void evaluate() {
        top_.eval();
        VerilatedVpi::callValueCbs();
    }

    void finalize() {
        top_.final();
    }

    const std::map<std::string, SignalInfo>& signals() const {
        return signals_by_display_;
    }

    SignalInfo* resolve_signal(const std::string& requested) {
        auto exact = signals_by_display_.find(requested);
        if (exact != signals_by_display_.end()) {
            return &exact->second;
        }

        if (!contains_dot_or_space(requested)) {
            const std::string root = top_vpi_scope();
            std::vector<std::string> top_ports = {
                root + "." + requested,
                root + "." + root + "." + requested,
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

        std::vector<std::string> candidates = candidate_vpi_names(requested);
        for (const auto& candidate : candidates) {
            vpiHandle handle = vpi_handle_by_name((PLI_BYTE8*)candidate.c_str(), nullptr);
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
            std::cerr << "[SERVER] Warning: signal '" << name
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
        vpi_put_value(signal->handle, &put_value, nullptr, vpiNoDelay);
        evaluate();
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

        vpiHandle root = vpi_handle_by_name((PLI_BYTE8*)top_vpi_scope().c_str(), nullptr);
        if (root != nullptr) {
            scan_ports(root);
            discover_scope(root);
        }

        // Verilator keeps externally writable ports in a synthetic "TOP"
        // scope outside normal vpiModule traversal. When the model instance
        // is also named TOP this scope is TOP.TOP. Probe discovered leaf names
        // there so stimuli do not target an internal read-only mirror.
        std::set<std::string> leaf_names;
        for (const auto& entry : signals_by_display_) {
            const std::string& vpi_name = entry.second.vpi_name;
            auto last_dot = vpi_name.rfind('.');
            if (last_dot != std::string::npos && last_dot + 1 < vpi_name.size()) {
                leaf_names.insert(vpi_name.substr(last_dot + 1));
            }
        }
        for (const auto& leaf_name : leaf_names) {
            std::vector<std::string> candidates = {
                leaf_name,
                top_display_scope() + "." + leaf_name,
                top_vpi_scope() + "." + leaf_name,
                top_vpi_scope() + ".TOP." + leaf_name,
            };
            for (const auto& candidate : candidates) {
                vpiHandle handle =
                    vpi_handle_by_name((PLI_BYTE8*)candidate.c_str(), nullptr);
                if (handle != nullptr) {
                    register_signal(handle);
                    break;
                }
            }
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
            if (!contains_dot_or_space(rest_dot)) {
                put_alias(rest_dot, signal.display_name);
            }
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
        const std::string synthetic_top_dot = root + ".TOP.";
        const std::string root_dot = root + ".";

        if (vpi_name == root) {
            return display_root;
        }
        if (starts_with(vpi_name, synthetic_top_dot)) {
            std::string rest = vpi_name.substr(synthetic_top_dot.size());
            return display_root + " " + replace_char(rest, '.', ' ');
        }
        if (starts_with(vpi_name, root_dot)) {
            std::string rest = vpi_name.substr(root_dot.size());
            return display_root + " " + replace_char(rest, '.', ' ');
        }
        return replace_char(vpi_name, '.', ' ');
    }

    std::string top_vpi_scope() const {
        return XSTR(TOP_VPI_SCOPE);
    }

    std::string top_display_scope() const {
        return XSTR(TOP_DISPLAY_SCOPE);
    }

    VerilatedContext& context_;
    TOP_CLASS& top_;
    uint64_t tick_unit_fs_ = 1;
    uint64_t sim_time_fs_ = 0;
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
    VerilatorVpiBackend& backend,
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

static bool evaluate_stop_conditions(const std::vector<json>& conditions,
                                     VerilatorVpiBackend& backend,
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

static socket_t open_loopback_listener(int& port) {
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
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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

int main(int argc, char** argv) {
    std::cerr << "[SERVER] Starting Verilator remote server" << std::endl;
    VerilatedContext context;
    context.threads(1);
    context.commandArgs(argc, argv);

    std::cerr << "[SERVER] Constructing top" << std::endl;
    std::unique_ptr<TOP_CLASS> top(new TOP_CLASS(&context, XSTR(TOP_VPI_SCOPE)));
    std::cerr << "[SERVER] Initializing VPI backend" << std::endl;
    VerilatorVpiBackend backend(context, *top);
    backend.initialize();
    std::cerr << "[SERVER] VPI backend initialized" << std::endl;

    std::cerr << "[SERVER] VPI items (" << backend.signals().size() << "):"
              << std::endl;
    for (const auto& entry : backend.signals()) {
        std::cerr << "  \"" << entry.first << "\" vpi=\""
                  << entry.second.vpi_name << "\" width="
                  << entry.second.width << std::endl;
    }

    int port = 0;
    socket_t listen_sock = open_loopback_listener(port);
    if (listen_sock == INVALID_SOCK) {
        std::cerr << "[SERVER] Failed to create listening socket" << std::endl;
        return 1;
    }

    std::cerr << "PORT:" << port << std::endl;
    std::cerr.flush();

    socket_t client = accept(listen_sock, nullptr, nullptr);
    close_socket(listen_sock);
    if (client == INVALID_SOCK) {
        std::cerr << "[SERVER] accept failed" << std::endl;
        return 1;
    }
    std::cerr << "[SERVER] Client connected" << std::endl;

    uint64_t sim_time_fs = 0;
    bool sim_running = false;
    bool sim_finished = false;
    std::priority_queue<StimulusEvent,
                        std::vector<StimulusEvent>,
                        StimulusCompare>
        stimulus_queue;
    std::set<std::pair<std::string, uint64_t>> scheduled_stimulus;
    uint64_t next_stimulus_sequence = 0;
    std::map<std::string, std::vector<TrackedSignal>> references;
    std::vector<Sample> recorded_samples;
    std::vector<char> read_buf;
    std::string msg;

    while (recv_message(client, msg, read_buf)) {
        json req;
        try {
            req = json::parse(msg);
        } catch (const json::parse_error& e) {
            send_error(client, json::object(), "INVALID_JSON",
                       std::string("JSON parse error: ") + e.what());
            continue;
        }

        std::string msg_type = req.value("type", "");
        if (msg_type == "greeting") {
            json greeting;
            greeting["type"] = "greeting";
            greeting["version"] = 0;
            greeting["protocol_version"] = 1;
            greeting["commands"] = json::array({
                "list_scopes", "list_items", "get_simulation_status",
                "query_interval", "reference_items", "run_simulation",
                "pause_simulation", "inject_stimulus", "inject_stimulus_v2"
            });
            greeting["events"] = json::array({
                "simulation_paused", "simulation_finished", "sample_batch"
            });
            greeting["features"] = {
                {"live_stimulus", true},
                {"wide_stimulus", true},
                {"streaming", true},
                {"pause_barrier", true},
            };
            send_message(client, greeting.dump());
            continue;
        }

        if (msg_type != "command") {
            send_error(client, req, "UNKNOWN_MESSAGE_TYPE",
                       "Unknown message type: " + msg_type);
            continue;
        }
        if (!req.contains("request_id") || !req["request_id"].is_string() ||
            req["request_id"].get<std::string>().empty()) {
            send_error(client, req, "MISSING_REQUEST_ID",
                       "protocol v1 command requires request_id");
            continue;
        }

        std::string command = req.value("command", "");

        if (command == "list_scopes") {
            std::map<std::string, json> scopes;
            scopes[""] = json::object();
            for (const auto& entry : backend.signals()) {
                const std::string& name = entry.first;
                auto last_space = name.rfind(' ');
                if (last_space != std::string::npos) {
                    scopes[name.substr(0, last_space)] = json::object();
                }
            }

            json resp;
            resp["type"] = "response";
            resp["command"] = "list_scopes";
            resp["scopes"] = json::object();
            for (const auto& entry : scopes) {
                resp["scopes"][entry.first] = entry.second;
            }
            send_response(client, req, resp);
        } else if (command == "list_items") {
            std::string scope_filter;
            if (req.contains("scope") && !req["scope"].is_null()) {
                scope_filter = req["scope"].get<std::string>();
            }

            json items = json::object();
            for (const auto& entry : backend.signals()) {
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

            json resp;
            resp["type"] = "response";
            resp["command"] = "list_items";
            resp["items"] = items;
            send_response(client, req, resp);
        } else if (command == "get_simulation_status") {
            json resp;
            resp["type"] = "response";
            resp["command"] = "get_simulation_status";
            resp["status"] = sim_finished ? "finished" :
                             (sim_running ? "running" : "paused");
            resp["latest_time"] = format_timestamp(sim_time_fs);
            send_response(client, req, resp);
        } else if (command == "reference_items") {
            std::string reference = req.value("reference", "DEFAULT");
            auto& tracked = references[reference];
            tracked.clear();

            if (req.contains("items") && req["items"].is_array()) {
                for (const auto& item_arr : req["items"]) {
                    if (!item_arr.is_array() || item_arr.empty()) {
                        continue;
                    }
                    std::string signal_name = item_arr[0].get<std::string>();
                    SignalInfo* signal = backend.resolve_signal(signal_name);
                    if (signal != nullptr) {
                        tracked.push_back(TrackedSignal{signal_name, signal});
                    } else {
                        std::cerr << "[SERVER] Warning: signal '"
                                  << signal_name << "' not found" << std::endl;
                    }
                }
            }

            std::cerr << "[SERVER] reference_items '" << reference
                      << "': " << tracked.size() << " signals" << std::endl;

            json resp;
            resp["type"] = "response";
            resp["command"] = "reference_items";
            send_response(client, req, resp);
        } else if (command == "inject_stimulus" ||
                   command == "inject_stimulus_v2") {
            std::vector<StimulusEvent> parsed;
            std::string code;
            std::string message;
            if (!parse_stimulus_request(req, sim_time_fs, false, backend,
                                        scheduled_stimulus,
                                        next_stimulus_sequence, parsed,
                                        code, message)) {
                send_error(client, req, code, message);
                continue;
            }
            enqueue_stimulus(parsed, stimulus_queue, scheduled_stimulus);
            json response = {
                {"type", "response"},
                {"command", command},
                {"count", parsed.size()},
            };
            send_response(client, req, response);
        } else if (command == "run_simulation") {
            if (!req.contains("run_id") || !req["run_id"].is_number_unsigned()) {
                send_error(client, req, "INVALID_RUN_ID",
                           "run_simulation requires an unsigned run_id");
                continue;
            }
            uint64_t active_run_id = req["run_id"].get<uint64_t>();
            uint64_t until_time_fs = std::numeric_limits<uint64_t>::max();
            if (req.contains("until_time") && !req["until_time"].is_null()) {
                until_time_fs = parse_timestamp(
                    req["until_time"].get<std::string>());
            }
            bool sample_item_values = req.value("sample_item_values", false);
            bool streaming = req.value("streaming", false);
            uint32_t batch_size = req.value("batch_size", (uint32_t)64);
            uint64_t batch_interval_fs = req.value(
                "batch_interval_fs", (uint64_t)10000000000ULL);
            uint64_t max_samples = req.value("max_samples", (uint64_t)1000000);

            std::vector<json> stop_conditions;
            if (req.contains("stop_conditions") &&
                req["stop_conditions"].is_array()) {
                for (const auto& condition : req["stop_conditions"]) {
                    stop_conditions.push_back(condition);
                }
            }

            std::vector<TrackedSignal>* tracked_ptr = nullptr;
            if (req.contains("items") && !req["items"].is_null()) {
                auto requested = references.find(req["items"].get<std::string>());
                if (requested != references.end() && !requested->second.empty()) {
                    tracked_ptr = &requested->second;
                }
            }
            if (!tracked_ptr) {
                for (auto& entry : references) {
                    if (!entry.second.empty()) {
                        tracked_ptr = &entry.second;
                        break;
                    }
                }
            }

            recorded_samples.clear();

            json ack;
            ack["type"] = "response";
            ack["command"] = "run_simulation";
            send_response(client, req, ack);

            sim_running = true;
            uint64_t total_samples = 0;
            std::string pause_cause = "until_time";
            json stop_detail;

            auto record_sample = [&]() -> bool {
                if (!sample_item_values || tracked_ptr == nullptr) {
                    return false;
                }
                Sample sample;
                sample.time_fs = sim_time_fs;
                for (const auto& tracked : *tracked_ptr) {
                    if (tracked.signal == nullptr) {
                        continue;
                    }
                    auto chunks = backend.read_chunks(*tracked.signal);
                    sample.item_values.insert(sample.item_values.end(),
                                              chunks.begin(),
                                              chunks.end());
                }
                if (recorded_samples.empty() ||
                    recorded_samples.back().item_values != sample.item_values) {
                    recorded_samples.push_back(std::move(sample));
                    ++total_samples;
                    return true;
                }
                return false;
            };

            auto apply_stimulus = [&]() {
                while (!stimulus_queue.empty() &&
                       stimulus_queue.top().time_fs <= sim_time_fs) {
                    StimulusEvent event = stimulus_queue.top();
                    stimulus_queue.pop();
                    scheduled_stimulus.erase({event.port, event.time_fs});
                    backend.write_chunks(event.port, event.chunks);
                }
            };

            bool connection_alive = true;
            auto pump_commands = [&]() -> bool {
                std::vector<std::string> messages;
                PollResult poll = poll_messages(client, read_buf, messages);
                if (poll == PollResult::Eof || poll == PollResult::Error) {
                    connection_alive = false;
                    return true;
                }
                bool pause = false;
                for (const auto& message_text : messages) {
                    json command_request = json::parse(message_text, nullptr, false);
                    if (command_request.is_discarded()) {
                        send_error(client, json::object(), "INVALID_JSON",
                                   "JSON parse error while simulation was running");
                        continue;
                    }
                    if (command_request.value("type", "") != "command" ||
                        !command_request.contains("request_id") ||
                        !command_request["request_id"].is_string()) {
                        send_error(client, command_request, "MISSING_REQUEST_ID",
                                   "protocol v1 command requires request_id");
                        continue;
                    }
                    if (!command_request.contains("run_id") ||
                        !command_request["run_id"].is_number_unsigned() ||
                        command_request["run_id"].get<uint64_t>() != active_run_id) {
                        send_error(client, command_request, "RUN_ID_MISMATCH",
                                   "command does not target the active run_id");
                        continue;
                    }
                    std::string incoming = command_request.value("command", "");
                    if (incoming == "pause_simulation") {
                        json response = {
                            {"type", "response"},
                            {"command", "pause_simulation"},
                            {"time", format_timestamp(sim_time_fs)},
                        };
                        send_response(client, command_request, response);
                        pause = true;
                    } else if (incoming == "inject_stimulus" ||
                               incoming == "inject_stimulus_v2") {
                        std::vector<StimulusEvent> parsed;
                        std::string code;
                        std::string message;
                        if (!parse_stimulus_request(
                                command_request, sim_time_fs, true, backend,
                                scheduled_stimulus, next_stimulus_sequence,
                                parsed, code, message)) {
                            send_error(client, command_request, code, message);
                            continue;
                        }
                        enqueue_stimulus(parsed, stimulus_queue,
                                         scheduled_stimulus);
                        json response = {
                            {"type", "response"},
                            {"command", incoming},
                            {"count", parsed.size()},
                        };
                        send_response(client, command_request, response);
                    } else {
                        send_error(client, command_request, "UNKNOWN_COMMAND",
                                   "unsupported command while simulation is running: " +
                                       incoming);
                    }
                }
                return pause;
            };

            std::vector<Sample> pending_batch;
            uint64_t last_batch_time_fs = sim_time_fs;
            if (streaming && record_sample()) {
                pending_batch.push_back(recorded_samples.back());
            }

            bool sim_aborted = false;
            while (sim_time_fs < until_time_fs && !backend.finished()) {
                if (pump_commands()) {
                    pause_cause = "client_pause";
                    sim_aborted = true;
                    break;
                }

                uint64_t next_stimulus_time =
                    stimulus_queue.empty()
                        ? std::numeric_limits<uint64_t>::max()
                        : stimulus_queue.top().time_fs;
                uint64_t next_internal_time = backend.next_internal_time_fs();
                uint64_t next_time = std::min(next_stimulus_time,
                                              next_internal_time);

                if (next_time == std::numeric_limits<uint64_t>::max()) {
                    sim_time_fs = until_time_fs;
                    backend.set_time_fs(sim_time_fs);
                    backend.evaluate();
                    break;
                }

                if (next_time > until_time_fs) {
                    sim_time_fs = until_time_fs;
                    backend.set_time_fs(sim_time_fs);
                    backend.evaluate();
                    break;
                }

                if (next_time < sim_time_fs) {
                    next_time = sim_time_fs;
                }
                sim_time_fs = next_time;
                backend.set_time_fs(sim_time_fs);
                apply_stimulus();
                backend.evaluate();

                bool new_sample = record_sample();
                if (total_samples >= max_samples) {
                    pause_cause = "max_samples";
                    sim_aborted = true;
                    break;
                }

                if (!stop_conditions.empty() &&
                    evaluate_stop_conditions(stop_conditions, backend,
                                             stop_detail)) {
                    pause_cause = "stop_condition";
                    sim_aborted = true;
                    break;
                }

                if (streaming && new_sample) {
                    pending_batch.push_back(recorded_samples.back());
                    bool time_trigger =
                        (sim_time_fs - last_batch_time_fs) >= batch_interval_fs;
                    bool size_trigger =
                        pending_batch.size() >= (size_t)batch_size;
                    if (time_trigger || size_trigger) {
                        json evt;
                        evt["type"] = "event";
                        evt["run_id"] = active_run_id;
                        evt["event"] = "sample_batch";
                        evt["samples"] = json::array();
                        for (const auto& sample : pending_batch) {
                            evt["samples"].push_back(encode_sample_json(sample));
                        }
                        send_message(client, evt.dump());
                        pending_batch.clear();
                        last_batch_time_fs = sim_time_fs;
                    }
                }
            }

            if (backend.finished()) {
                sim_finished = true;
                pause_cause = "until_time";
            }

            if (connection_alive && streaming && !pending_batch.empty()) {
                json evt;
                evt["type"] = "event";
                evt["run_id"] = active_run_id;
                evt["event"] = "sample_batch";
                evt["samples"] = json::array();
                for (const auto& sample : pending_batch) {
                    evt["samples"].push_back(encode_sample_json(sample));
                }
                send_message(client, evt.dump());
            }

            sim_running = false;
            if (!connection_alive) {
                std::cerr << "[SERVER] Client EOF while simulation was running"
                          << std::endl;
                break;
            }
            std::cerr << "[SERVER] Simulation paused at "
                      << sim_time_fs / FEMTOS_PER_NS << " ns, "
                      << recorded_samples.size() << " samples, cause="
                      << pause_cause << (sim_aborted ? " (aborted)" : "")
                      << std::endl;

            json evt;
            evt["type"] = "event";
            evt["run_id"] = active_run_id;
            evt["event"] = "simulation_paused";
            evt["time"] = format_timestamp(sim_time_fs);
            evt["cause"] = pause_cause;
            if (!stop_detail.is_null()) {
                evt["stop_detail"] = stop_detail;
            }
            send_message(client, evt.dump());
        } else if (command == "query_interval") {
            uint64_t start_fs = 0;
            uint64_t end_fs = std::numeric_limits<uint64_t>::max();
            if (req.contains("interval") && req["interval"].is_array() &&
                req["interval"].size() == 2) {
                start_fs = parse_timestamp(req["interval"][0].get<std::string>());
                end_fs = parse_timestamp(req["interval"][1].get<std::string>());
            }

            json samples_json = json::array();
            for (const auto& sample : recorded_samples) {
                if (sample.time_fs < start_fs || sample.time_fs > end_fs) {
                    continue;
                }
                samples_json.push_back(encode_sample_json(sample));
            }

            json resp;
            resp["type"] = "response";
            resp["command"] = "query_interval";
            resp["samples"] = samples_json;
            send_response(client, req, resp);
        } else if (command == "pause_simulation") {
            sim_running = false;
            json resp;
            resp["type"] = "response";
            resp["command"] = "pause_simulation";
            resp["time"] = format_timestamp(sim_time_fs);
            send_response(client, req, resp);
        } else {
            send_error(client, req, "UNKNOWN_COMMAND",
                       "Unknown command: " + command);
        }
    }

    std::cerr << "[SERVER] Client disconnected, exiting" << std::endl;
    close_socket(client);
    backend.finalize();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
