#include "hushrate_core_impl.h"
#include "hushrate_keys.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace fs = std::filesystem;
extern char** environ;

namespace {

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------------ utils

std::string jsonEscape(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += (char)c;
        }
    }
    return o;
}
std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
std::string jbool(bool b) { return b ? "true" : "false"; }
std::string numC(double v, int prec = 1) { std::ostringstream o; o.imbue(std::locale::classic()); o << std::fixed << std::setprecision(prec) << v; return o.str(); }

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> v;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(sep, a);
        if (b == std::string::npos) { v.push_back(s.substr(a)); break; }
        v.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return v;
}
std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool endsWith(const std::string& s, const std::string& e)
{
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string baseName(const std::string& p)
{
    size_t i = p.find_last_of('/');
    return i == std::string::npos ? p : p.substr(i + 1);
}
std::string homeDir()
{
    const char* h = getenv("HOME");
    return h ? h : "/tmp";
}
std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool writeFile(const std::string& path, const std::string& data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << data;
    return (bool)f;
}

// ---------------------------------------------------------------- SHA-256

struct Sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t buf[64];
    size_t blen = 0;
    uint64_t total = 0;
    static uint32_t rr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rr(e, 6) ^ rr(e, 11) ^ rr(e, 25), ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rr(a, 2) ^ rr(a, 13) ^ rr(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t n)
    {
        total += n;
        while (n) {
            size_t t = std::min(n, 64 - blen);
            std::memcpy(buf + blen, p, t);
            blen += t; p += t; n -= t;
            if (blen == 64) { block(buf); blen = 0; }
        }
    }
    std::string hex()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t z = 0;
        while (blen != 56) update(&z, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; ++i) l[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(l, 8);
        char out[65];
        for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return out;
    }
};

std::string sha256File(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    Sha256 s;
    std::vector<char> b(65536);
    while (f) {
        f.read(b.data(), (std::streamsize)b.size());
        s.update((const uint8_t*)b.data(), (size_t)f.gcount());
    }
    return s.hex();
}

// ------------------------------------------------------------- processes


struct Child {
    pid_t pid = -1;
    int fdOut = -1;
    int fdErr = -1;
    std::string bufOut, bufErr;
    bool exited = false;
    int code = 0;
};

void reapChild(Child& c)
{
    if (c.exited || c.pid <= 0) return;
    int st = 0;
    if (waitpid(c.pid, &st, WNOHANG) == c.pid) {
        c.exited = true;
        c.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    }
}

std::string findExe(const std::string& name)
{
    if (name.find('/') != std::string::npos) return name;
    std::string path = getenv("PATH") ? getenv("PATH") : "";
    path += ":/usr/bin:/bin:/usr/local/bin";
    for (auto& d : split(path, ':')) {
        if (d.empty()) continue;
        std::string p = d + "/" + name;
        if (access(p.c_str(), X_OK) == 0) return p;
    }
    return "";
}

// Environment without library-injection variables (Nix / AppImage hosts may
// set them, and they can break system tools like curl or gpg).
std::vector<std::string> cleanEnv()
{
    std::vector<std::string> e;
    for (char** p = environ; p && *p; ++p) {
        std::string s = *p;
        if (s.rfind("LD_LIBRARY_PATH=", 0) == 0 || s.rfind("LD_PRELOAD=", 0) == 0) continue;
        e.push_back(s);
    }
    return e;
}

bool spawn(Child& c, const std::string& exe, const std::vector<std::string>& args, const std::string& cwd,
           bool dieWithParent, bool mergeErr, std::string& err)
{
    std::vector<std::string> av = { exe };
    av.insert(av.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : av) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> env = cleanEnv();
    std::vector<char*> envp;
    for (auto& s : env) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);

    int po[2], pe[2] = { -1, -1 };
    if (pipe(po) != 0) { err = std::string("pipe: ") + std::strerror(errno); return false; }
    if (!mergeErr && pipe(pe) != 0) { close(po[0]); close(po[1]); err = std::string("pipe: ") + std::strerror(errno); return false; }
    int execErrPipe[2];   // reports exec failure to the parent
    if (pipe(execErrPipe) != 0) { err = "pipe failed"; return false; }
    fcntl(execErrPipe[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) { err = std::string("fork: ") + std::strerror(errno); return false; }
    if (pid == 0) {
        setpgid(0, 0);
#ifdef __linux__
        if (dieWithParent) prctl(PR_SET_PDEATHSIG, SIGKILL);
#else
        (void)dieWithParent;
#endif
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) { dup2(dn, STDIN_FILENO); close(dn); }
        dup2(po[1], STDOUT_FILENO);
        if (mergeErr) dup2(po[1], STDERR_FILENO);
        else dup2(pe[1], STDERR_FILENO);
        close(po[0]); close(po[1]);
        if (!mergeErr) { close(pe[0]); close(pe[1]); }
        close(execErrPipe[0]);
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) { int e = errno; (void)!write(execErrPipe[1], &e, sizeof e); _exit(126); }
        execve(exe.c_str(), argv.data(), envp.data());
        int e = errno;
        (void)!write(execErrPipe[1], &e, sizeof e);
        _exit(127);
    }
    close(po[1]);
    if (!mergeErr) close(pe[1]);
    close(execErrPipe[1]);
    int e = 0;
    ssize_t n = read(execErrPipe[0], &e, sizeof e);
    close(execErrPipe[0]);
    if (n == (ssize_t)sizeof e) {
        int st; waitpid(pid, &st, 0);
        close(po[0]); if (!mergeErr) close(pe[0]);
        err = "cannot execute " + exe + ": " + std::strerror(e);
        return false;
    }
    c.pid = pid;
    c.fdOut = po[0];
    c.fdErr = mergeErr ? -1 : pe[0];
    fcntl(c.fdOut, F_SETFL, fcntl(c.fdOut, F_GETFL) | O_NONBLOCK);
    if (c.fdErr >= 0) fcntl(c.fdErr, F_SETFL, fcntl(c.fdErr, F_GETFL) | O_NONBLOCK);
    return true;
}

// Reads what is available; appends complete lines to `lines`. Returns false on EOF of stdout.
bool pump(Child& c, std::vector<std::string>& lines)
{
    bool alive = true;
    char b[4096];
    for (int* fd : { &c.fdOut, &c.fdErr }) {
        if (*fd < 0) continue;
        std::string& buf = (fd == &c.fdOut) ? c.bufOut : c.bufErr;
        for (;;) {
            ssize_t n = read(*fd, b, sizeof b);
            if (n > 0) buf.append(b, (size_t)n);
            else if (n == 0) { if (fd == &c.fdOut) alive = false; close(*fd); *fd = -1; break; }
            else break;
        }
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string l = buf.substr(0, pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (fd == &c.fdOut) lines.push_back(l);
            buf.erase(0, pos + 1);
        }
    }
    return alive;
}

void closeChild(Child& c)
{
    if (c.fdOut >= 0) close(c.fdOut);
    if (c.fdErr >= 0) close(c.fdErr);
    c.fdOut = c.fdErr = -1;
}

// SIGTERM to the process group, wait, then SIGKILL.
void terminate(Child& c, int graceMs)
{
    if (c.pid <= 0) return;
    reapChild(c);
    if (c.exited) return;
    kill(-c.pid, SIGTERM);
    auto t0 = Clock::now();
    while (!c.exited && std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < graceMs) {
        usleep(100 * 1000);
        reapChild(c);
    }
    if (!c.exited) {
        kill(-c.pid, SIGKILL);
        for (int i = 0; i < 40 && !c.exited; ++i) { usleep(50 * 1000); reapChild(c); }
    }
}

// Runs a short-lived tool and captures stdout/stderr.
int runCapture(const std::string& exe, const std::vector<std::string>& args, int timeoutSec,
               std::string& out, std::string& errOut, const std::string& cwd = "")
{
    Child c;
    std::string err;
    out.clear(); errOut.clear();
    if (!spawn(c, exe, args, cwd, false, false, err)) { errOut = err; return -1; }
    auto t0 = Clock::now();
    std::string soFar;
    for (;;) {
        struct pollfd p[2]; int np = 0;
        if (c.fdOut >= 0) p[np++] = { c.fdOut, POLLIN, 0 };
        if (c.fdErr >= 0) p[np++] = { c.fdErr, POLLIN, 0 };
        if (np) poll(p, (nfds_t)np, 200);
        char b[8192];
        if (c.fdOut >= 0) for (;;) {
            ssize_t n = read(c.fdOut, b, sizeof b);
            if (n > 0) { if (out.size() < (8u << 20)) out.append(b, (size_t)n); }
            else { if (n == 0) { close(c.fdOut); c.fdOut = -1; } break; }
        }
        if (c.fdErr >= 0) for (;;) {
            ssize_t n = read(c.fdErr, b, sizeof b);
            if (n > 0) { if (errOut.size() < 65536) errOut.append(b, (size_t)n); }
            else { if (n == 0) { close(c.fdErr); c.fdErr = -1; } break; }
        }
        reapChild(c);
        if (c.exited && c.fdOut < 0 && c.fdErr < 0) break;
        if (c.exited && np == 0) break;
        if (std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - t0).count() > timeoutSec) {
            kill(-c.pid, SIGKILL);
            int st; waitpid(c.pid, &st, 0);
            closeChild(c);
            errOut += "\n(timeout)";
            return -2;
        }
    }
    if (!c.exited) { int st; waitpid(c.pid, &st, 0); c.code = WIFEXITED(st) ? WEXITSTATUS(st) : 128; }
    closeChild(c);
    return c.code;
}

// Opens a non-blocking TCP connection and waits up to timeoutMs. Calls onConnected(fd) for each
// address that connects, until it returns true. Returns whether it did.
template <class F>
bool withConnection(const std::string& host, const std::string& port, int timeoutMs, F onConnected)
{
    struct addrinfo hints; std::memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) return false;
    bool ok = false;
    for (auto* a = res; a && !ok; a = a->ai_next) {
        int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
        int r = connect(s, a->ai_addr, a->ai_addrlen);
        if (r != 0 && errno == EINPROGRESS) {
            struct pollfd p = { s, POLLOUT, 0 };
            if (poll(&p, 1, timeoutMs) == 1) {
                int e = 0; socklen_t l = sizeof e;
                getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l);
                r = e ? -1 : 0;
            } else r = -1;
        }
        if (r == 0) ok = onConnected(s);
        close(s);
    }
    freeaddrinfo(res);
    return ok;
}

bool tcpConnect(const std::string& host, const std::string& port, int timeoutMs)
{
    return withConnection(host, port, timeoutMs, [](int) { return true; });
}

// Real ZMQ check: a plain TCP accept is not enough (firewalls/proxies accept
// and then drop). Sends the ZMTP signature and expects one back (0xFF first).
bool zmqProbe(const std::string& host, const std::string& port, int timeoutMs)
{
    return withConnection(host, port, timeoutMs, [timeoutMs](int s) {
        static const unsigned char sig[10] = { 0xFF, 0, 0, 0, 0, 0, 0, 0, 1, 0x7F };
        if (send(s, sig, sizeof sig, MSG_NOSIGNAL) != (ssize_t)sizeof sig) return false;
        struct pollfd p = { s, POLLIN, 0 };
        if (poll(&p, 1, timeoutMs) != 1) return false;
        unsigned char b = 0;
        return recv(s, &b, 1, 0) == 1 && b == 0xFF;
    });
}

bool portFree(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return true;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a; std::memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    bool ok = bind(s, (sockaddr*)&a, sizeof a) == 0;
    close(s);
    return ok;
}


// ------------------------------------------------------------ binary specs

struct Spec {
    std::string name;                     // xmrig / p2pool
    std::string repo;                     // xmrig/xmrig
    std::vector<std::string> patterns;    // asset name must contain one of these
    std::string sumsName, sigName;        // sigName empty => clearsigned sumsName
    std::string keyText, fingerprint;
};

const Spec& specFor(const std::string& n)
{
    static const Spec xm = { "xmrig", "xmrig/xmrig",
        { "linux-static-x64", "linux-musl-x64", "linux-x64", "linux-x86_64" },
        "SHA256SUMS", "SHA256SUMS.sig", kXmrigKey, kXmrigFingerprint };
    static const Spec pp = { "p2pool", "SChernykh/p2pool",
        { "linux-x64", "linux-x86_64", "linux-amd64" },
        "sha256sums.txt.asc", "", kP2poolKey, kP2poolFingerprint };
    return n == "xmrig" ? xm : pp;
}

// ------------------------------------------------------------------ state

std::mutex g_mu;                           // guards everything below
std::string g_binDir;
std::string g_phase = "idle", g_node, g_hash, g_shares, g_err;
int g_p2poolShares = 0;
std::string g_h60, g_h15, g_pauseReason;
bool g_paused = false;
Clock::time_point g_startedAt;
std::deque<std::string> g_lines;
size_t g_lineBase = 0;                     // index of g_lines.front()
std::atomic<bool> g_stop{ false };
std::atomic<bool> g_mining{ false };
std::thread g_miner;

std::vector<std::string> g_jobLog;
bool g_jobRunning = false, g_jobOk = false;
std::thread g_job;

std::string binDir()
{
    std::lock_guard<std::mutex> l(g_mu);
    return g_binDir.empty() ? homeDir() + "/.local/share/hushrate/bin" : g_binDir;
}
std::string dataDir() { return homeDir() + "/.local/share/hushrate/data"; }

void clog(const std::string& line)
{
    std::lock_guard<std::mutex> l(g_mu);
    g_lines.push_back(line);
    while (g_lines.size() > 600) { g_lines.pop_front(); ++g_lineBase; }
}
void setPhase(const std::string& p) { std::lock_guard<std::mutex> l(g_mu); g_phase = p; }
void jlog(const std::string& line) { std::lock_guard<std::mutex> l(g_mu); g_jobLog.push_back(line); }

// --------------------------------------------------------------- manifest
// <binDir>/.hushrate-integrity.txt : "name<TAB>sha256<TAB>tag<TAB>asset" per line

struct Entry { std::string sha, tag, asset; };

std::map<std::string, Entry> readManifest(const std::string& dir)
{
    std::map<std::string, Entry> m;
    std::istringstream in(readFile(dir + "/.hushrate-integrity.txt"));
    std::string l;
    while (std::getline(in, l)) {
        auto f = split(l, '\t');
        if (f.size() >= 4) m[f[0]] = { f[1], f[2], f[3] };
    }
    return m;
}
void writeManifest(const std::string& dir, const std::map<std::string, Entry>& m)
{
    std::string o;
    for (auto& kv : m) o += kv.first + "\t" + kv.second.sha + "\t" + kv.second.tag + "\t" + kv.second.asset + "\n";
    writeFile(dir + "/.hushrate-integrity.txt", o);
}

bool isVerified(const std::string& dir, const std::string& name, std::string* tag = nullptr)
{
    std::string path = dir + "/" + name;
    if (!fs::exists(path)) return false;
    auto m = readManifest(dir);
    auto it = m.find(name);
    if (it == m.end()) return false;
    if (tag) *tag = it->second.tag;
    return sha256File(path) == it->second.sha;
}

std::string versionOf(const std::string& path)
{
    std::string out, err;
    if (runCapture(path, { "--version" }, 5, out, err) != 0) return "";
    return trim(out.substr(0, out.find('\n')));
}

// ----------------------------------------------------------- sums parsing

std::map<std::string, std::string> parseSums(const std::string& text)
{
    std::map<std::string, std::string> h;
    std::string cur;
    std::istringstream in(text);
    std::string line;
    auto isHex64 = [](const std::string& s) {
        return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); });
    };
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.rfind("Name:", 0) == 0) cur = trim(s.substr(5));
        else if (s.rfind("SHA256:", 0) == 0 && !cur.empty()) { h[cur] = trim(s.substr(7)); cur.clear(); }
    }
    in.clear(); in.str(text);
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s.rfind("Name:", 0) == 0 || s.rfind("SHA256:", 0) == 0 ||
            s.rfind("Size:", 0) == 0 || s.rfind("-----", 0) == 0) continue;
        size_t sp = s.find_first_of(" \t");
        if (sp == std::string::npos) continue;
        std::string hash = s.substr(0, sp), name = trim(s.substr(sp));
        if (!name.empty() && name[0] == '*') name.erase(0, 1);
        if (isHex64(hash)) h[name] = hash;
    }
    return h;
}

// -------------------------------------------------------------------- GPG

struct Gpg {
    std::string home, gpg, err;
    bool ok = false;
    Gpg(const std::string& keyText)
    {
        gpg = findExe("gpg");
        if (gpg.empty()) { err = "gpg is not installed (e.g. 'sudo dnf install gnupg2')"; return; }
        char tmpl[] = "/tmp/hushrate-gpg-XXXXXX";
        if (!mkdtemp(tmpl)) { err = "cannot create temp dir"; return; }
        home = tmpl;
        chmod(home.c_str(), 0700);
        writeFile(home + "/key.asc", keyText);
        std::string o, e;
        if (runCapture(gpg, { "--homedir", home, "--batch", "--import", home + "/key.asc" }, 30, o, e) != 0) {
            err = "could not import the pinned GPG key: " + e; return;
        }
        ok = true;
    }
    ~Gpg()
    {
        if (!home.empty()) {
            std::string o, e;
            std::string gc = findExe("gpgconf");   // stop the gpg-agent started for this temp homedir
            if (!gc.empty()) runCapture(gc, { "--homedir", home, "--kill", "gpg-agent" }, 5, o, e);
            std::error_code ec; fs::remove_all(home, ec);
        }
    }
    // Checks VALIDSIG came from the expected fingerprint.
    bool verify(const std::vector<std::string>& files, const std::string& fpr, std::string& why)
    {
        std::vector<std::string> a = { "--homedir", home, "--batch", "--status-fd", "1", "--verify" };
        a.insert(a.end(), files.begin(), files.end());
        std::string o, e;
        runCapture(gpg, a, 60, o, e);
        std::string found;
        bool good = false;
        std::istringstream in(o);
        std::string l;
        while (std::getline(in, l)) {
            if (l.rfind("[GNUPG:] GOODSIG", 0) == 0) good = true;
            if (l.rfind("[GNUPG:] VALIDSIG", 0) == 0) {
                auto p = split(l, ' ');
                if (p.size() >= 3) found = p[2];
            }
        }
        if (!good) { why = "invalid GPG signature: " + o + e; return false; }
        std::string up = found, ex = fpr;
        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
        std::transform(ex.begin(), ex.end(), ex.begin(), ::toupper);
        if (up.empty() || !endsWith(up, ex)) {
            why = "valid signature but NOT from the expected key (expected ..." + fpr + ", found " + found + ")";
            return false;
        }
        return true;
    }
    std::string decrypt(const std::string& file)
    {
        std::string o, e;
        runCapture(gpg, { "--homedir", home, "--batch", "--decrypt", file }, 60, o, e);
        return o;
    }
};

// ---------------------------------------------------------------- install

struct InstallOpts {
    std::string apiBase = "https://api.github.com";
    bool allowHttp = false;   // tests only
};

bool curlGet(const std::string& url, const std::string& dest, bool allowHttp, std::string& body, std::string& why)
{
    std::string curl = findExe("curl");
    if (curl.empty()) { why = "curl is not installed"; return false; }
    std::vector<std::string> a = { "-fsSL", "--max-time", dest.empty() ? "30" : "600",
                                   "--proto", allowHttp ? "=http,https" : "=https",
                                   "--proto-redir", allowHttp ? "=http,https" : "=https",
                                   "-H", "User-Agent: hushrate-basecamp" };
    if (url.find("api.github.com") != std::string::npos || url.find("/repos/") != std::string::npos) {
        a.push_back("-H"); a.push_back("Accept: application/vnd.github+json");
    }
    if (!dest.empty()) { a.push_back("-o"); a.push_back(dest); }
    a.push_back(url);
    std::string o, e;
    int rc = runCapture(curl, a, dest.empty() ? 40 : 620, o, e);
    if (rc != 0) { why = "download failed (" + url + "): " + trim(e); return false; }
    if (dest.empty()) body = o;
    return true;
}

bool installOne(const Spec& sp, const std::string& dir, const InstallOpts& io, std::string& why)
{
    jlog("[" + sp.name + "] looking up latest release of " + sp.repo);
    std::string json;
    if (!curlGet(io.apiBase + "/repos/" + sp.repo + "/releases/latest", "", io.allowHttp, json, why)) return false;

    std::smatch m;
    std::string tag;
    if (std::regex_search(json, m, std::regex("\"tag_name\"\\s*:\\s*\"([^\"]+)\""))) tag = m[1];
    if (tag.empty()) { why = "no tag_name in release data"; return false; }
    std::vector<std::string> urls;
    {
        std::regex re("\"browser_download_url\"\\s*:\\s*\"([^\"]+)\"");
        for (auto it = std::sregex_iterator(json.begin(), json.end(), re); it != std::sregex_iterator(); ++it)
            urls.push_back((*it)[1]);
    }
    std::string assetUrl, assetName;
    for (auto& pat : sp.patterns) {
        for (auto& u : urls) {
            std::string n = baseName(u);
            if (n.find(pat) != std::string::npos && endsWith(n, ".tar.gz") && n.find("debug") == std::string::npos) {
                assetUrl = u; assetName = n; break;
            }
        }
        if (!assetUrl.empty()) break;
    }
    if (assetUrl.empty()) { why = "no matching Linux x64 asset in release " + tag; return false; }
    auto urlOf = [&](const std::string& n) { for (auto& u : urls) if (baseName(u) == n) return u; return std::string(); };
    std::string sumsUrl = urlOf(sp.sumsName), sigUrl = sp.sigName.empty() ? "" : urlOf(sp.sigName);
    if (sumsUrl.empty() || (!sp.sigName.empty() && sigUrl.empty())) { why = "checksum/signature files missing in release " + tag; return false; }

    char tmpl[] = "/tmp/hushrate-dl-XXXXXX";
    if (!mkdtemp(tmpl)) { why = "cannot create temp dir"; return false; }
    std::string tmp = tmpl;
    struct Cleanup { std::string d; ~Cleanup() { std::error_code ec; fs::remove_all(d, ec); } } cleanup{ tmp };

    std::string dummy;
    jlog("[" + sp.name + "] downloading " + assetName + " (" + tag + ")");
    if (!curlGet(assetUrl, tmp + "/" + assetName, io.allowHttp, dummy, why)) return false;
    if (!curlGet(sumsUrl, tmp + "/" + sp.sumsName, io.allowHttp, dummy, why)) return false;
    if (!sigUrl.empty() && !curlGet(sigUrl, tmp + "/" + sp.sigName, io.allowHttp, dummy, why)) return false;

    jlog("[" + sp.name + "] verifying GPG signature and SHA256");
    Gpg g(sp.keyText);
    if (!g.ok) { why = g.err; return false; }
    std::string sumsText;
    if (sp.sigName.empty()) {
        if (!g.verify({ tmp + "/" + sp.sumsName }, sp.fingerprint, why)) return false;
        sumsText = g.decrypt(tmp + "/" + sp.sumsName);
    } else {
        if (!g.verify({ tmp + "/" + sp.sigName, tmp + "/" + sp.sumsName }, sp.fingerprint, why)) return false;
        sumsText = readFile(tmp + "/" + sp.sumsName);
    }
    auto sums = parseSums(sumsText);
    auto it = sums.find(assetName);
    if (it == sums.end()) { why = "'" + assetName + "' not found in the signed checksums"; return false; }
    std::string actual = sha256File(tmp + "/" + assetName);
    std::string exp = it->second;
    std::transform(exp.begin(), exp.end(), exp.begin(), ::tolower);
    if (actual != exp) { why = "SHA256 mismatch for " + assetName + " (expected " + exp + ", got " + actual + ")"; return false; }

    jlog("[" + sp.name + "] extracting");
    std::string tar = findExe("tar");
    if (tar.empty()) { why = "tar is not installed"; return false; }
    fs::create_directories(tmp + "/x");
    std::string o, e;
    if (runCapture(tar, { "-xzf", tmp + "/" + assetName, "-C", tmp + "/x" }, 120, o, e) != 0) { why = "tar failed: " + e; return false; }
    std::string found;
    for (auto& p : fs::recursive_directory_iterator(tmp + "/x"))
        if (p.is_regular_file() && p.path().filename() == sp.name) { found = p.path().string(); break; }
    if (found.empty()) { why = "binary " + sp.name + " not found in archive"; return false; }

    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string target = dir + "/" + sp.name, staged = target + ".new";
    fs::copy_file(found, staged, fs::copy_options::overwrite_existing, ec);
    if (ec) { why = "cannot write " + staged + ": " + ec.message(); return false; }
    chmod(staged.c_str(), 0755);
    if (rename(staged.c_str(), target.c_str()) != 0) { why = std::string("rename failed: ") + std::strerror(errno); fs::remove(staged, ec); return false; }

    auto man = readManifest(dir);
    man[sp.name] = { sha256File(target), tag, assetName };
    writeManifest(dir, man);
    jlog("[" + sp.name + "] installed and verified: " + tag);
    return true;
}

// ----------------------------------------------------------------- mining

struct Node { std::string host; int rpc = 0, zmq = 0; };

const std::regex kHashRe("speed\\s+\\S+/\\S+/\\S+\\s+([\\d.]+|n/a)");
const std::regex kSpeedRe("speed\\s+\\S+/\\S+/\\S+\\s+(?:[\\d.]+|n/a)\\s+([\\d.]+|n/a)\\s+([\\d.]+|n/a)");
const std::regex kSharesRe("accepted\\s*\\(\\s*(\\d+)\\s*/\\s*(\\d+)\\s*\\)");

enum class NodeResult { Stopped, NodeFailed, Exited };


// P2Pool prints hundreds of these per minute (sync, every new block/job); they bury real messages.
bool noisyLine(const std::string& l)
{
    static const char* pats[] = { "SideChain verified block at height", "SideChain add_block:", "BlockTemplate base",
                                  "BlockTemplate final", "StratumServer sent new job", "SideChain new chain tip",
                                  "RandomX_Hasher couldn't allocate", "net      new job from" };
    for (const char* p : pats) if (l.find(p) != std::string::npos) return true;
    return false;
}

void appendLine(const char* tag, const std::string& l)
{
    if (noisyLine(l)) return;
    clog(std::string("[") + tag + "] " + l);
}

// CPU temperature in C (-1 unknown) and "on battery" detection, used by the safety limits.
double cpuTempC()
{
#ifdef HUSHRATE_TEST
    if (const char* f = std::getenv("HUSHRATE_FAKE_TEMP")) { std::string v = trim(readFile(f)); if (!v.empty()) return std::atof(v.c_str()); }
#endif
    double temp = -1;
    std::error_code ec;
    for (const char* want : { "coretemp", "k10temp", "zenpower", "cpu_thermal" }) {
        for (auto& d : fs::directory_iterator("/sys/class/hwmon", ec)) {
            if (trim(readFile((d.path() / "name").string())) != want) continue;
            for (const char* f : { "temp1_input", "temp2_input" }) {
                std::string v = trim(readFile((d.path() / f).string()));
                if (!v.empty()) { temp = std::atof(v.c_str()) / 1000.0; break; }
            }
            if (temp >= 0) break;
        }
        if (temp >= 0) break;
    }
    return temp;
}

bool onBattery()
{
#ifdef HUSHRATE_TEST
    if (const char* f = std::getenv("HUSHRATE_FAKE_BAT")) return trim(readFile(f)) == "1";
#endif
    std::error_code ec;
    bool haveBat = false, acOnline = false, haveAc = false;
    for (auto& d : fs::directory_iterator("/sys/class/power_supply", ec)) {
        std::string ty = trim(readFile((d.path() / "type").string()));
        if (ty == "Battery") { if (trim(readFile((d.path() / "scope").string())) != "Device") haveBat = true; }
        else if (ty == "Mains") { haveAc = true; if (trim(readFile((d.path() / "online").string())) == "1") acOnline = true; }
    }
    return haveBat && haveAc && !acOnline;
}

struct Safety { int maxTemp = 0; bool silent = false; bool pauseBat = false; };

NodeResult runNode(const Node& n, const std::string& wallet, int threads, bool light, const std::string& sidechain,
                   const std::string& p2pool, const std::string& xmrig, const Safety& sf)
{
    std::vector<std::string> pa = { "--host", n.host, "--rpc-port", std::to_string(n.rpc), "--zmq-port", std::to_string(n.zmq),
                                    "--wallet", wallet, "--stratum", "127.0.0.1:3333", "--no-color" };
    if (light) pa.push_back("--light-mode");
    if (sidechain == "mini") pa.push_back("--mini");
    else if (sidechain == "nano") pa.push_back("--nano");

    std::error_code ec;
    fs::create_directories(dataDir(), ec);

    Child pp, xm;
    std::string err;
    if (!spawn(pp, p2pool, pa, dataDir(), true, true, err)) { clog("[core] cannot start p2pool: " + err); return NodeResult::NodeFailed; }
    setPhase("p2pool");
    clog("[core] P2Pool started (pid " + std::to_string(pp.pid) + ") with node " + n.host);

    bool xmrigUp = false;
    Clock::time_point xmrigAt = Clock::now();
    std::vector<double> zmqTimes;
    auto t0 = Clock::now();
    auto secs = [&](Clock::time_point a) { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a).count() / 1000.0; };
    NodeResult result = NodeResult::Stopped;
    bool paused = false; bool pausedByTemp = false;
    Clock::time_point lastSafety = Clock::now();

    for (;;) {
        std::vector<std::string> lp, lx;
        struct pollfd p[2]; int np = 0;
        if (pp.fdOut >= 0) p[np++] = { pp.fdOut, POLLIN, 0 };
        if (xm.fdOut >= 0) p[np++] = { xm.fdOut, POLLIN, 0 };
        if (np) poll(p, (nfds_t)np, 200); else usleep(200 * 1000);

        pump(pp, lp);
        if (xmrigUp) pump(xm, lx);
        reapChild(pp);
        if (xmrigUp) reapChild(xm);

        for (auto& l : lp) {
            appendLine("p2pool", l);
            if (!xmrigUp && l.find("TCPServer listening on 127.0.0.1:3333") != std::string::npos) {
                std::vector<std::string> xa = { "-a", "rx/0", "-o", "127.0.0.1:3333", "-u", wallet, "-t", std::to_string(threads),
                                                "--no-color", "--print-time", "5" };
                if (sf.silent) { xa.push_back("--cpu-priority"); xa.push_back("0"); }
                if (spawn(xm, xmrig, xa, dataDir(), true, true, err)) {
                    xmrigUp = true;
                    xmrigAt = Clock::now();
                    { std::lock_guard<std::mutex> lk(g_mu); g_node = n.host + ":" + std::to_string(n.rpc); g_phase = "mining"; g_err.clear(); g_startedAt = Clock::now(); g_paused = false; g_pauseReason.clear(); g_h60.clear(); g_h15.clear(); }
                    clog("[core] XMRig started (pid " + std::to_string(xm.pid) + ")");
                } else {
                    clog("[core] cannot start xmrig: " + err);
                    result = NodeResult::Exited;
                }
            }
            if (l.find("ZMQReader failed to connect") != std::string::npos || l.find("Couldn't start ZMQ reader") != std::string::npos) {
                clog("[core] P2Pool could not use this node's ZMQ; trying the next node");
                result = NodeResult::NodeFailed;
            }
            if (l.find("StratumServer SHARE FOUND") != std::string::npos) { std::lock_guard<std::mutex> lk(g_mu); ++g_p2poolShares; }
            if (l.find("ZMQ is not running, restarting it") != std::string::npos) {
                double now = secs(t0);
                zmqTimes.push_back(now);
                zmqTimes.erase(std::remove_if(zmqTimes.begin(), zmqTimes.end(), [&](double t) { return now - t > 30; }), zmqTimes.end());
                if (zmqTimes.size() >= 3) {
                    clog("[core] node " + n.host + " keeps restarting its ZMQ connection; trying the next node");
                    result = NodeResult::NodeFailed;
                }
            }
        }
        for (auto& l : lx) {
            appendLine("xmrig", l);
            std::smatch m;
            if (!paused && std::regex_search(l, m, kHashRe) && m[1] != "n/a") { std::lock_guard<std::mutex> lk(g_mu); g_hash = m[1]; }
            if (!paused && std::regex_search(l, m, kSpeedRe)) { std::lock_guard<std::mutex> lk(g_mu); if (m[1] != "n/a") g_h60 = m[1]; if (m[2] != "n/a") g_h15 = m[2]; }
            if (std::regex_search(l, m, kSharesRe)) { std::lock_guard<std::mutex> lk(g_mu); g_shares = std::string(m[1]) + "/" + std::string(m[2]); }
        }

        if (xmrigUp && !xm.exited && (sf.maxTemp > 0 || sf.pauseBat) && secs(lastSafety) >= 2.0) {
            lastSafety = Clock::now();
            double tc = sf.maxTemp > 0 ? cpuTempC() : -1;
            bool bat = sf.pauseBat && onBattery();
            bool hot = sf.maxTemp > 0 && tc >= sf.maxTemp;
            if (!paused && (hot || bat)) {
                kill(-xm.pid, SIGSTOP);
                paused = true; pausedByTemp = hot && !bat;
                std::string why = bat ? "running on battery" : "CPU at " + numC(tc, 0) + " C (limit " + std::to_string(sf.maxTemp) + " C)";
                clog("[core] mining paused: " + why);
                std::lock_guard<std::mutex> lk(g_mu); g_phase = "paused"; g_paused = true; g_pauseReason = why; g_hash = "0"; g_h60 = "0"; g_h15 = "0";
            } else if (paused && !bat && (sf.maxTemp <= 0 || (tc >= 0 && tc <= sf.maxTemp - 8) || (!pausedByTemp && !hot))) {
                kill(-xm.pid, SIGCONT);
                paused = false;
                clog("[core] mining resumed");
                std::lock_guard<std::mutex> lk(g_mu); g_phase = "mining"; g_paused = false; g_pauseReason.clear();
            }
        }
        if (g_stop) { result = NodeResult::Stopped; break; }
        if (result == NodeResult::NodeFailed || (result == NodeResult::Exited)) break;
        if (pp.exited) {
            clog("[core] P2Pool exited (code " + std::to_string(pp.code) + ")");
            // Dying soon after XMRig started means the node/ZMQ was bad, not a normal end.
            result = (!xmrigUp || secs(xmrigAt) < 40) ? NodeResult::NodeFailed : NodeResult::Exited;
            if (result == NodeResult::NodeFailed) clog("[core] P2Pool died right after starting; trying the next node");
            break;
        }
        if (xmrigUp && xm.exited) {
            clog("[core] XMRig exited (code " + std::to_string(xm.code) + ")");
            result = NodeResult::Exited;
            break;
        }
        if (!xmrigUp && secs(t0) > 60) {
            clog("[core] P2Pool did not become ready within 60 s with " + n.host);
            result = NodeResult::NodeFailed;
            break;
        }
    }

    setPhase("stopping");
    if (xm.pid > 0 && paused) kill(-xm.pid, SIGCONT);   // a stopped process would never see SIGTERM
    if (xm.pid > 0) { terminate(xm, 5000); std::vector<std::string> r; pump(xm, r); for (auto& l : r) appendLine("xmrig", l); closeChild(xm); }
    terminate(pp, 10000);
    { std::vector<std::string> r; pump(pp, r); for (auto& l : r) appendLine("p2pool", l); closeChild(pp); }
    return result;
}

bool validWallet(const std::string& w)
{
    static const std::string b58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    if (w.size() != 95 || w[0] != '4') return false;
    return std::all_of(w.begin(), w.end(), [](char c) { return b58.find(c) != std::string::npos; });
}

std::vector<Node> parseNodes(const std::string& s)
{
    std::vector<Node> v;
    std::string norm = s;
    for (auto& ch : norm) if (ch == ';' || ch == ' ' || ch == '\t' || ch == '\r') ch = ',';
    for (auto& e : split(norm, ',')) {
        std::string ee = trim(e);
        if (ee.rfind("http://", 0) == 0) ee = ee.substr(7);
        while (!ee.empty() && ee.back() == '/') ee.pop_back();
        auto f = split(ee, ':');
        if (f.empty() || trim(f[0]).empty()) continue;
        Node n; n.host = trim(f[0]); n.rpc = f.size() >= 2 ? std::atoi(f[1].c_str()) : 18081;
        n.zmq = f.size() >= 3 ? std::atoi(f[2].c_str()) : 18083;
        if (n.rpc > 0 && n.zmq > 0) v.push_back(n);
    }
    return v;
}

std::string g_dynamicUrl = "https://xmr.ditatompel.com/api/v1/nodes?protocol=http&sort_by=last_checked&sort_direction=desc&limit=60";
bool g_dynamicAllowHttp = false;

// Parses the public node list (flat JSON objects inside "items") without a JSON library.
std::vector<Node> parseDynamicNodes(const std::string& json)
{
    std::vector<Node> out;
    size_t a = json.find("\"items\"");
    if (a == std::string::npos) return out;
    a = json.find('[', a);
    if (a == std::string::npos) return out;
    int depth = 0; size_t start = 0;
    for (size_t i = a + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') { for (++i; i < json.size() && json[i] != '"'; ++i) if (json[i] == '\\') ++i; continue; }
        if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}') {
            if (--depth == 0) {
                std::string o = json.substr(start, i - start + 1);
                auto str = [&](const char* k) { std::smatch m; std::regex re(std::string("\"") + k + "\"\\s*:\\s*\"([^\"]*)\""); return std::regex_search(o, m, re) ? std::string(m[1]) : std::string(); };
                auto num = [&](const char* k) { std::smatch m; std::regex re(std::string("\"") + k + "\"\\s*:\\s*(-?[0-9.]+)"); return std::regex_search(o, m, re) ? std::atof(std::string(m[1]).c_str()) : -1.0; };
                auto boo = [&](const char* k) { std::smatch m; std::regex re(std::string("\"") + k + "\"\\s*:\\s*(true|false)"); return std::regex_search(o, m, re) ? std::string(m[1]) == "true" : false; };
                std::string host = str("hostname"); if (host.empty()) host = str("ip");
                double port = num("port");
                bool tor = boo("is_tor"), i2p = boo("is_i2p");
                if (str("nettype") == "mainnet" && boo("is_available") && !tor && !i2p && num("uptime") >= 70 && !host.empty() && port > 0) {
                    Node n; n.host = host; n.rpc = (int)port; n.zmq = 18083; out.push_back(n);
                }
            }
            if (depth < 0) break;
        } else if (c == ']' && depth == 0) break;
    }
    return out;
}

struct Probe { Node n; bool ok = false; double ms = 0; std::string why; };

struct ScanCache { bool valid = false; std::string key; std::vector<Probe> usable; Clock::time_point at; size_t probed = 0, rpcFail = 0, zmqFail = 0; };
ScanCache g_scan;
std::mutex g_scanMu;
std::atomic<bool> g_scanRunning{ false };
std::thread g_scanThread;

void probeOne(Probe& p)
{
    auto t0 = Clock::now();
    if (!tcpConnect(p.n.host, std::to_string(p.n.rpc), 3500)) { p.why = "RPC unreachable"; return; }
    p.ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    if (zmqProbe(p.n.host, std::to_string(p.n.zmq), 3500)) { p.ok = true; return; }
    int alt = p.n.zmq == 18083 ? 18084 : 18083;
    if (zmqProbe(p.n.host, std::to_string(alt), 3500)) { p.n.zmq = alt; p.ok = true; return; }
    p.why = "no working ZMQ";
}

// Probes all candidates in parallel batches.
void probeAll(std::vector<Probe>& v)
{
    const size_t B = 16;
    for (size_t i = 0; i < v.size() && !g_stop; i += B) {
        std::vector<std::thread> ts;
        for (size_t j = i; j < std::min(v.size(), i + B); ++j) ts.emplace_back([&v, j]() { probeOne(v[j]); });
        for (auto& t : ts) t.join();
    }
}

// ---------------------------------------------------------- system info

std::string sysStatsJson()
{
    static std::mutex mu;
    static unsigned long long pTotal = 0, pIdle = 0;
    std::lock_guard<std::mutex> l(mu);
    double cpu = -1;
    {
        std::ifstream f("/proc/stat");
        std::string tag; unsigned long long u = 0, n = 0, sy = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
        f >> tag >> u >> n >> sy >> id >> io >> irq >> sirq >> st;
        unsigned long long idle = id + io, total = u + n + sy + id + io + irq + sirq + st;
        if (pTotal && total > pTotal) cpu = 100.0 * (double)((total - pTotal) - (idle - pIdle)) / (double)(total - pTotal);
        pTotal = total; pIdle = idle;
    }
    double ram = -1;
    {
        std::ifstream f("/proc/meminfo");
        std::string k; unsigned long long v; std::string unit;
        unsigned long long tot = 0, avail = 0;
        while (f >> k >> v >> unit) { if (k == "MemTotal:") tot = v; else if (k == "MemAvailable:") avail = v; }
        if (tot) ram = 100.0 * (double)(tot - avail) / (double)tot;
    }
    double temp = cpuTempC();
    return "{\"cpu\":" + numC(cpu) + ",\"ram\":" + numC(ram) + ",\"temp\":" + numC(temp) + "}";
}

std::string hwInfoJson()
{
    std::set<std::pair<std::string, std::string>> phys;
    int logical = 0;
    {
        std::istringstream in(readFile("/proc/cpuinfo"));
        std::string line, pid = "0", cid = "";
        while (std::getline(in, line)) {
            size_t c = line.find(':');
            if (c == std::string::npos) { if (!cid.empty() || logical) {} continue; }
            std::string k = trim(line.substr(0, c)), v = trim(line.substr(c + 1));
            if (k == "processor") { ++logical; cid = ""; }
            else if (k == "physical id") pid = v;
            else if (k == "core id") { cid = v; phys.insert({ pid, cid }); }
        }
    }
    int cores = phys.empty() ? std::max(1, logical) : (int)phys.size();
    if (logical < 1) logical = cores;
    double ramGb = 0;
    {
        std::istringstream in(readFile("/proc/meminfo"));
        std::string k, unit; unsigned long long v;
        while (in >> k >> v >> unit) if (k == "MemTotal:") { ramGb = (double)v / 1048576.0; break; }
    }
    bool huge = std::atoi(trim(readFile("/proc/sys/vm/nr_hugepages")).c_str()) > 0;
    bool msr = fs::exists("/dev/cpu/0/msr");
    std::string l3t = trim(readFile("/sys/devices/system/cpu/cpu0/cache/index3/size"));   // e.g. "16384K"
    int l3kb = std::atoi(l3t.c_str());
    if (!l3t.empty() && l3t.back() == 'M') l3kb *= 1024;
    int byCache = l3kb > 0 ? std::max(1, l3kb / 2048) : cores;   // RandomX: ~2 MB of L3 per thread
    int rec = cores <= 2 ? 1 : std::max(1, std::min(byCache, cores - 1));
    rec = std::min(rec, std::max(1, logical));
    int hugeNow = std::atoi(trim(readFile("/proc/sys/vm/nr_hugepages")).c_str());
    std::string thp = trim(readFile("/sys/kernel/mm/transparent_hugepage/enabled"));
    bool onBat = onBattery();
    return "{\"cores\":" + std::to_string(cores) + ",\"logical\":" + std::to_string(logical) + ",\"ramGb\":" + numC(ramGb) +
           ",\"hugePages\":" + jbool(huge) + ",\"msr\":" + jbool(msr) + ",\"recommended\":" + std::to_string(rec) +
           ",\"hugeNow\":" + std::to_string(hugeNow) + ",\"hugeWant\":1280,\"l3kb\":" + std::to_string(l3kb) +
           ",\"thp\":" + jstr(thp) + ",\"battery\":" + jbool(onBat) + "}";
}

std::string configPath() { return homeDir() + "/.local/share/hushrate/config.txt"; }

std::string scanKey(const std::vector<Node>& nodes, bool dyn)
{
    std::string k = dyn ? "D|" : "S|";
    for (auto& n : nodes) k += n.host + ":" + std::to_string(n.rpc) + ":" + std::to_string(n.zmq) + ",";
    return k;
}

// Builds the ordered list of usable nodes: the user's nodes first (in order), then the public
// list by latency. Only nodes with working RPC and a real ZMQ handshake are returned.
struct ScanStats { size_t probed = 0, rpcFail = 0, zmqFail = 0; };
std::vector<Probe> buildUsable(const std::vector<Node>& nodes, bool useDynamic, bool verbose, ScanStats* st = nullptr)
{
    std::vector<Node> cands = nodes;
    if (useDynamic && !g_stop) {
        if (verbose) clog("[core] fetching the public node list");
        std::string json, why;
        if (curlGet(g_dynamicUrl, "", g_dynamicAllowHttp, json, why)) {
            auto dyn = parseDynamicNodes(json);
            if (verbose) clog("[core] public list: " + std::to_string(dyn.size()) + " candidate nodes");
            for (auto& d : dyn) {
                bool dup = false;
                for (auto& c : cands) if (c.host == d.host && c.rpc == d.rpc) dup = true;
                if (!dup) cands.push_back(d);
            }
        } else if (verbose) clog("[core] could not fetch the public list: " + why);
    }
    if (cands.size() > 90) cands.resize(90);
    if (verbose) clog("[core] probing " + std::to_string(cands.size()) + " nodes (RPC + real ZMQ handshake)...");
    std::vector<Probe> probes(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) probes[i].n = cands[i];
    probeAll(probes);
    std::vector<Probe> usable;
    if (st) { st->probed = probes.size(); for (auto& p : probes) if (!p.ok) { if (p.why == "RPC unreachable") ++st->rpcFail; else ++st->zmqFail; } }
    for (auto& p : probes) if (p.ok) usable.push_back(p);
    std::stable_sort(usable.begin(), usable.end(), [&](const Probe& a, const Probe& b) {
        bool ua = false, ub = false;
        for (auto& n : nodes) { if (n.host == a.n.host && n.rpc == a.n.rpc) ua = true; if (n.host == b.n.host && n.rpc == b.n.rpc) ub = true; }
        if (ua != ub) return ua;
        return ua ? false : a.ms < b.ms;
    });
    if (verbose) clog("[core] " + std::to_string(usable.size()) + " of " + std::to_string(probes.size()) + " nodes have working RPC and ZMQ");
    return usable;
}

void minerThread(std::string wallet, int threads, std::vector<Node> nodes, bool light, std::string sidechain, bool useDynamic, Safety sf)
{
    std::string dir = binDir();
    std::string xmrig = dir + "/xmrig", p2pool = dir + "/p2pool";
    auto fail = [&](const std::string& why) {
        clog("[core] " + why);
        { std::lock_guard<std::mutex> l(g_mu); g_phase = "error"; g_err = why; }
        g_mining = false;
    };
    // Re-verify what is on disk before running it.
    for (auto* nm : { "xmrig", "p2pool" })
        if (!isVerified(dir, nm)) return fail(std::string(nm) + " is missing or does not match its recorded SHA256 (install/verify it first)");
    if (!portFree(3333)) return fail("port 127.0.0.1:3333 is already in use (another P2Pool?)");

    setPhase("node");
    std::vector<Probe> usable;
    {
        std::string key = scanKey(nodes, useDynamic);
        std::lock_guard<std::mutex> lk(g_scanMu);
        bool fresh = g_scan.valid && g_scan.key == key && !g_scan.usable.empty() &&
                     std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_scan.at).count() < 300;
        if (fresh) { usable = g_scan.usable; clog("[core] using the node scan from a moment ago (" + std::to_string(usable.size()) + " usable nodes)"); }
    }
    if (usable.empty()) {
        ScanStats ss;
        usable = buildUsable(nodes, useDynamic, true, &ss);
        std::lock_guard<std::mutex> lk(g_scanMu);
        g_scan.probed = ss.probed; g_scan.rpcFail = ss.rpcFail; g_scan.zmqFail = ss.zmqFail;
        g_scan.key = scanKey(nodes, useDynamic); g_scan.usable = usable; g_scan.valid = true; g_scan.at = Clock::now();
    }

    bool ran = false;
    for (auto& pr : usable) {
        if (g_stop) break;
        const Node& use = pr.n;
        clog("[core] trying node " + use.host + ":" + std::to_string(use.rpc) + " (zmq " + std::to_string(use.zmq) + ", " + std::to_string((int)pr.ms) + " ms)");
        NodeResult r = runNode(use, wallet, threads, light, sidechain, p2pool, xmrig, sf);
        ran = true;
        if (r == NodeResult::Stopped || r == NodeResult::Exited) break;
        ran = false;
    }
    if (g_stop) { setPhase("idle"); clog("[core] stopped"); }
    else if (!ran) return fail("no usable node (RPC + ZMQ) was found. Public nodes rarely expose ZMQ; running your own node with --zmq-pub is the reliable option");
    else { setPhase("stopped"); clog("[core] mining ended"); }
    g_mining = false;
}


// ---------------------------------------------------------- update check
// Looks up the latest XMRig / P2Pool releases (read only; nothing is downloaded or installed here).
struct UpdInfo { std::string cur, latest; bool present = false; };
std::mutex g_updMu;
std::map<std::string, UpdInfo> g_upd;
bool g_updRunning = false, g_updDone = false;
std::string g_updErr;
std::thread g_updThread;
std::string g_apiBase = "https://api.github.com";
bool g_apiAllowHttp = false;
std::map<std::string, std::string> g_repoOverride;   // tests only

void updateWorker()
{
    std::string dir = binDir();
    auto man = readManifest(dir);
    std::map<std::string, UpdInfo> res;
    std::string err;
    for (const char* nm : { "xmrig", "p2pool" }) {
        UpdInfo u;
        u.present = fs::exists(dir + "/" + nm);
        auto it = man.find(nm);
        if (it != man.end()) u.cur = it->second.tag;
        std::string repo = g_repoOverride.count(nm) ? g_repoOverride[nm] : specFor(nm).repo;
        std::string json, why;
        if (curlGet(g_apiBase + "/repos/" + repo + "/releases/latest", "", g_apiAllowHttp, json, why)) {
            std::smatch m;
            if (std::regex_search(json, m, std::regex("\"tag_name\"\\s*:\\s*\"([^\"]+)\""))) u.latest = m[1];
        } else err = why;
        res[nm] = u;
    }
    std::lock_guard<std::mutex> l(g_updMu);
    g_upd = res; g_updErr = err; g_updRunning = false; g_updDone = true;
}

// Runs when the library is unloaded: never leave miners behind on a clean exit.
struct Guard {
    ~Guard()
    {
        g_stop = true;
        if (g_miner.joinable()) g_miner.join();
        if (g_job.joinable()) g_job.join();
        g_stop = true;
        if (g_scanThread.joinable()) g_scanThread.join();
        if (g_updThread.joinable()) g_updThread.join();
    }
};
Guard g_guard;

} // namespace

// ============================================================== public API

std::string HushrateCoreImpl::ping() { return "ok"; }

std::string HushrateCoreImpl::setBinDir(const std::string& dir)
{
    std::lock_guard<std::mutex> l(g_mu);
    g_binDir = trim(dir);
    return jstr("bin dir = " + (g_binDir.empty() ? homeDir() + "/.local/share/hushrate/bin" : g_binDir));
}

std::string HushrateCoreImpl::binStatus()
{
    std::string dir = binDir();
    std::ostringstream o;
    o << "{\"binDir\":" << jstr(dir)
      << ",\"tools\":{\"curl\":" << jbool(!findExe("curl").empty()) << ",\"gpg\":" << jbool(!findExe("gpg").empty())
      << ",\"tar\":" << jbool(!findExe("tar").empty()) << "}";
    for (auto* nm : { "xmrig", "p2pool" }) {
        std::string path = dir + "/" + nm, tag;
        bool present = fs::exists(path);
        bool ver = present && isVerified(dir, nm, &tag);
        o << "," << jstr(nm) << ":{\"present\":" << jbool(present) << ",\"verified\":" << jbool(ver)
          << ",\"tag\":" << jstr(tag) << ",\"version\":" << jstr(present ? versionOf(path) : "") << "}";
    }
    o << "}";
    return o.str();
}

std::string HushrateCoreImpl::binInstall(const std::string& which)
{
    std::vector<std::string> names;
    if (which == "all") names = { "xmrig", "p2pool" };
    else if (which == "xmrig" || which == "p2pool") names = { which };
    else return "{\"error\":\"which must be xmrig, p2pool or all\"}";
    if (g_mining) return "{\"error\":\"stop mining before installing/updating binaries\"}";
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_jobRunning) return "{\"error\":\"an install job is already running\"}";
        g_jobRunning = true; g_jobOk = false; g_jobLog.clear();
    }
    if (g_job.joinable()) g_job.join();
    std::string dir = binDir();
    g_job = std::thread([names, dir]() {
        bool all = true;
        for (auto& n : names) {
            std::string why;
            if (!installOne(specFor(n), dir, InstallOpts(), why)) { jlog("[" + n + "] FAILED: " + why); all = false; }
        }
        std::lock_guard<std::mutex> l(g_mu);
        g_jobOk = all; g_jobRunning = false;
        g_jobLog.push_back(all ? "[done] all verified" : "[done] finished with errors");
    });
    return "{\"started\":true}";
}

std::string HushrateCoreImpl::binTrust(const std::string& which)
{
    if (which != "xmrig" && which != "p2pool") return "{\"error\":\"which must be xmrig or p2pool\"}";
    std::string dir = binDir(), path = dir + "/" + which;
    if (!fs::exists(path)) return "{\"error\":\"" + jsonEscape(path) + " does not exist\"}";
    auto m = readManifest(dir);
    m[which] = { sha256File(path), "(trusted by user)", "" };
    writeManifest(dir, m);
    return "{\"trusted\":true}";
}

std::string HushrateCoreImpl::jobState()
{
    std::lock_guard<std::mutex> l(g_mu);
    std::string o = "{\"running\":" + jbool(g_jobRunning) + ",\"ok\":" + jbool(g_jobOk) + ",\"log\":[";
    for (size_t i = 0; i < g_jobLog.size(); ++i) o += (i ? "," : "") + jstr(g_jobLog[i]);
    return o + "]}";
}

std::string HushrateCoreImpl::mineStart(const std::string& cfg)
{
    std::map<std::string, std::string> kv;
    for (auto& l : split(cfg, '\n')) {
        size_t e = l.find('=');
        if (e != std::string::npos) kv[trim(l.substr(0, e))] = trim(l.substr(e + 1));
    }
    std::string wallet = kv["wallet"];
    if (!validWallet(wallet)) return "{\"error\":\"wallet must be a 95-character primary Monero address starting with 4\"}";
    int threads = std::atoi(kv["threads"].c_str());
    if (threads < 1 || threads > 256) return "{\"error\":\"threads must be between 1 and 256\"}";
    auto nodes = parseNodes(kv["nodes"]);
    bool dynamic = kv["dynamic"] == "1";
    if (nodes.empty() && !dynamic) return "{\"error\":\"no nodes given (host:rpcport:zmqport, comma separated) and public list disabled\"}";
    std::string side = kv["sidechain"];
    if (side != "mini" && side != "nano") side = "main";
    bool light = kv["light"] == "1";
    Safety sf;
    sf.maxTemp = std::atoi(kv["maxtemp"].c_str());
    if (sf.maxTemp != 0 && (sf.maxTemp < 50 || sf.maxTemp > 100)) return "{\"error\":\"maxtemp must be 0 (off) or between 50 and 100\"}";
    sf.silent = kv["silent"] == "1";
    sf.pauseBat = kv["pausebat"] == "1";
    if (sf.silent) threads = std::max(1, threads / 2);

    if (g_mining) return "{\"error\":\"already running\"}";
    {
        std::lock_guard<std::mutex> l(g_mu);
        if (g_jobRunning) return "{\"error\":\"an install job is running\"}";
    }
    if (g_miner.joinable()) g_miner.join();
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_phase = "starting"; g_node.clear(); g_hash.clear(); g_shares.clear(); g_err.clear(); g_p2poolShares = 0; g_h60.clear(); g_h15.clear(); g_paused = false; g_pauseReason.clear();
    }
    g_stop = false;
    g_mining = true;
    clog("[core] starting: " + std::to_string(threads) + " threads, sidechain " + side + (light ? ", light mode" : "") + (sf.silent ? ", silent mode" : "") + (sf.maxTemp ? ", temp limit " + std::to_string(sf.maxTemp) + " C" : "") + (sf.pauseBat ? ", pause on battery" : ""));
    g_miner = std::thread(minerThread, wallet, threads, nodes, light, side, dynamic, sf);
    return "{\"started\":true}";
}


std::string HushrateCoreImpl::sysStats()
{
    return sysStatsJson();
}

std::string HushrateCoreImpl::hwInfo()
{
    return hwInfoJson();
}

std::string HushrateCoreImpl::cfgLoad()
{
    return jstr(readFile(configPath()));
}

std::string HushrateCoreImpl::cfgSave(const std::string& text)
{
    if (text.size() > 65536) return "{\"error\":\"config too large\"}";
    std::error_code ec;
    fs::create_directories(fs::path(configPath()).parent_path(), ec);
    std::string tmp = configPath() + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return std::string("{\"error\":") + jstr(std::string("cannot write config: ") + std::strerror(errno)) + "}";
    size_t off = 0;
    while (off < text.size()) { ssize_t n = write(fd, text.data() + off, text.size() - off); if (n <= 0) break; off += (size_t)n; }
    close(fd);
    if (off != text.size() || rename(tmp.c_str(), configPath().c_str()) != 0) return "{\"error\":\"cannot save config\"}";
    return "{\"saved\":true}";
}

std::string HushrateCoreImpl::updateCheck()
{
    std::lock_guard<std::mutex> l(g_updMu);
    if (g_updRunning) return "{\"started\":false,\"note\":\"already checking\"}";
    if (g_updThread.joinable()) g_updThread.join();
    g_updRunning = true;
    g_updThread = std::thread(updateWorker);
    return "{\"started\":true}";
}

std::string HushrateCoreImpl::updateState()
{
    std::lock_guard<std::mutex> l(g_updMu);
    std::string o = "{\"running\":" + jbool(g_updRunning) + ",\"done\":" + jbool(g_updDone) + ",\"error\":" + jstr(g_updErr);
    for (const char* nm : { "xmrig", "p2pool" }) {
        auto it = g_upd.find(nm);
        UpdInfo u; if (it != g_upd.end()) u = it->second;
        bool newer = u.present && !u.cur.empty() && !u.latest.empty() && u.cur != u.latest;
        o += std::string(",") + jstr(nm) + ":{\"present\":" + jbool(u.present) + ",\"cur\":" + jstr(u.cur) + ",\"latest\":" + jstr(u.latest) + ",\"newer\":" + jbool(newer) + "}";
    }
    return o + "}";
}

std::string HushrateCoreImpl::nodeScan(const std::string& cfg)
{
    if (g_mining) return "{\"error\":\"mining is running\"}";
    if (g_scanRunning) return "{\"started\":false,\"note\":\"already scanning\"}";
    std::map<std::string, std::string> kv;
    for (auto& l : split(cfg, '\n')) {
        size_t e = l.find('=');
        if (e != std::string::npos) kv[trim(l.substr(0, e))] = trim(l.substr(e + 1));
    }
    auto nodes = parseNodes(kv["nodes"]);
    bool dyn = kv["dynamic"] == "1";
    if (nodes.empty() && !dyn) return "{\"error\":\"no nodes to scan\"}";
    if (g_scanThread.joinable()) g_scanThread.join();
    g_scanRunning = true;
    g_stop = false;
    g_scanThread = std::thread([nodes, dyn]() {
        ScanStats ss;
        auto usable = buildUsable(nodes, dyn, false, &ss);
        std::lock_guard<std::mutex> lk(g_scanMu);
        g_scan.probed = ss.probed; g_scan.rpcFail = ss.rpcFail; g_scan.zmqFail = ss.zmqFail;
        g_scan.key = scanKey(nodes, dyn); g_scan.usable = usable; g_scan.valid = true; g_scan.at = Clock::now();
        g_scanRunning = false;
    });
    return "{\"started\":true}";
}

std::string HushrateCoreImpl::nodeScanState()
{
    std::lock_guard<std::mutex> lk(g_scanMu);
    std::string o = "{\"running\":" + jbool(g_scanRunning) + ",\"valid\":" + jbool(g_scan.valid) + ",\"count\":" + std::to_string(g_scan.usable.size());
    if (!g_scan.usable.empty()) {
        auto& b = g_scan.usable[0];
        o += ",\"best\":{\"host\":" + jstr(b.n.host) + ",\"rpc\":" + std::to_string(b.n.rpc) + ",\"zmq\":" + std::to_string(b.n.zmq) + ",\"ms\":" + std::to_string((int)b.ms) + "}";
    }
    o += ",\"probed\":" + std::to_string(g_scan.probed) + ",\"rpcFail\":" + std::to_string(g_scan.rpcFail) + ",\"zmqFail\":" + std::to_string(g_scan.zmqFail) + ",\"nodes\":[";
    for (size_t i = 0; i < g_scan.usable.size() && i < 8; ++i) {
        auto& b = g_scan.usable[i];
        o += (i ? "," : "") + std::string("{\"host\":") + jstr(b.n.host) + ",\"rpc\":" + std::to_string(b.n.rpc) + ",\"zmq\":" + std::to_string(b.n.zmq) + ",\"ms\":" + std::to_string((int)b.ms) + "}";
    }
    return o + "]}";
}

std::string HushrateCoreImpl::mineStop()
{
    if (!g_mining) return "{\"stopped\":false,\"note\":\"not running\"}";
    g_stop = true;
    return "{\"stopping\":true}";
}

std::string HushrateCoreImpl::mineState()
{
    std::lock_guard<std::mutex> l(g_mu);
    return "{\"phase\":" + jstr(g_phase) + ",\"node\":" + jstr(g_node) + ",\"hashrate\":" + jstr(g_hash) +
           ",\"shares\":" + jstr(g_shares) + ",\"p2poolShares\":" + std::to_string(g_p2poolShares) +
           ",\"error\":" + jstr(g_err) + ",\"running\":" + jbool(g_mining) +
           ",\"h60\":" + jstr(g_h60) + ",\"h15\":" + jstr(g_h15) + ",\"paused\":" + jbool(g_paused) + ",\"pauseReason\":" + jstr(g_pauseReason) +
           ",\"uptime\":" + std::to_string((g_mining && g_phase == "mining") || g_paused ? (long)std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_startedAt).count() : 0L) + "}";
}

std::string HushrateCoreImpl::mineLog(const std::string& since)
{
    size_t from = (size_t)std::max(0L, std::atol(since.c_str()));
    std::lock_guard<std::mutex> l(g_mu);
    size_t start = std::max(from, g_lineBase);
    std::string o = "{\"next\":" + std::to_string(g_lineBase + g_lines.size()) + ",\"lines\":[";
    for (size_t i = start; i < g_lineBase + g_lines.size(); ++i)
        o += (i > start ? "," : "") + jstr(g_lines[i - g_lineBase]);
    return o + "]}";
}

#ifdef HUSHRATE_TEST
// Test hooks (not compiled into the shipped module).
bool hushrateTestInstall(const std::string& name, const std::string& repo, const std::vector<std::string>& pats,
                         const std::string& sums, const std::string& sig, const std::string& key,
                         const std::string& fpr, const std::string& dir, const std::string& apiBase, std::string& why)
{
    Spec sp = { name, repo, pats, sums, sig, key, fpr };
    InstallOpts io; io.apiBase = apiBase; io.allowHttp = true;
    return installOne(sp, dir, io, why);
}
void hushrateTestDynamic(const std::string& url) { g_dynamicUrl = url; g_dynamicAllowHttp = true; }
void hushrateTestUpdate(const std::string& api, const std::string& rx, const std::string& rp) { g_apiBase = api; g_apiAllowHttp = true; g_repoOverride["xmrig"] = rx; g_repoOverride["p2pool"] = rp; }
size_t hushrateTestParseNodes(const std::string& j, std::string& first) { auto v = parseDynamicNodes(j); if (!v.empty()) first = v[0].host + ":" + std::to_string(v[0].rpc); return v.size(); }
std::string hushrateTestSha(const std::string& p) { return sha256File(p); }
std::map<std::string, std::string> hushrateTestSums(const std::string& t) { return parseSums(t); }
#endif
