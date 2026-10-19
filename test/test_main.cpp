#include "minimal_impl.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <thread>
#include <unistd.h>
bool hushrateTestInstall(const std::string&, const std::string&, const std::vector<std::string>&, const std::string&, const std::string&,
                         const std::string&, const std::string&, const std::string&, const std::string&, std::string&);
void hushrateTestUpdate(const std::string&, const std::string&, const std::string&);
std::string hushrateTestSha(const std::string&);
void hushrateTestDynamic(const std::string&);
size_t hushrateTestParseNodes(const std::string&, std::string&);
std::map<std::string, std::string> hushrateTestSums(const std::string&);
static int fails = 0;
#define CHECK(c, msg) do { bool ok_ = (c); std::cout << (ok_ ? "PASS " : "FAIL ") << msg << "\n"; if (!ok_) ++fails; } while (0)
static std::string rd(const std::string& p) { std::ifstream f(p); return std::string((std::istreambuf_iterator<char>(f)), {}); }
static bool has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

int main(int argc, char** argv)
{
    std::string W = argv[1];     // work dir prepared by run_tests.sh
    std::string port = argv[2];
    std::string api = "http://127.0.0.1:" + port;
    MinimalImpl m;

    // sha + sums
    CHECK(hushrateTestSha(W + "/files/data.bin") == rd(W + "/data.sha"), "sha256 matches sha256sum");
    auto s = hushrateTestSums("# c\nabcdef\nName: a.tar.gz\nSHA256: " + std::string(64, 'a') + "\nSize: 1\n" +
                              std::string(64, 'b') + " *b.tar.gz\n");
    CHECK(s["a.tar.gz"] == std::string(64, 'a') && s["b.tar.gz"] == std::string(64, 'b'), "parseSums both formats");

    // install ok (detached sig, xmrig style)
    std::string why, key = rd(W + "/pub.asc"), fpr = rd(W + "/fpr.txt");
    while (!fpr.empty() && (fpr.back() == '\n')) fpr.pop_back();
    bool ok = hushrateTestInstall("xmrig", "t/xmrig", { "linux-static-x64" }, "SHA256SUMS", "SHA256SUMS.sig", key, fpr, W + "/bin1", api, why);
    CHECK(ok, "install xmrig-style (detached sig) " + why);
    CHECK(access((W + "/bin1/xmrig").c_str(), X_OK) == 0, "xmrig binary installed + executable");
    CHECK(has(rd(W + "/bin1/.hushrate-integrity.txt"), "v9.9.9"), "manifest records tag");
    // install ok (clearsigned, p2pool style)
    ok = hushrateTestInstall("p2pool", "t/p2pool", { "linux-x64" }, "sha256sums.txt.asc", "", key, fpr, W + "/bin1", api, why);
    CHECK(ok, "install p2pool-style (clearsigned) " + why);
    // wrong fingerprint
    ok = hushrateTestInstall("xmrig", "t/xmrig", { "linux-static-x64" }, "SHA256SUMS", "SHA256SUMS.sig", key, "0000000000000000000000000000000000000000", W + "/bin2", api, why);
    CHECK(!ok && has(why, "NOT from the expected key"), "wrong pinned fingerprint rejected: " + why);
    // tampered archive
    ok = hushrateTestInstall("xmrig", "t/bad", { "linux-static-x64" }, "SHA256SUMS", "SHA256SUMS.sig", key, fpr, W + "/bin3", api, why);
    CHECK(!ok && has(why, "SHA256 mismatch"), "tampered archive rejected: " + why);
    CHECK(access((W + "/bin3/xmrig").c_str(), F_OK) != 0, "nothing installed after failure");
    // bad signature (sums altered)
    ok = hushrateTestInstall("xmrig", "t/badsig", { "linux-static-x64" }, "SHA256SUMS", "SHA256SUMS.sig", key, fpr, W + "/bin4", api, why);
    CHECK(!ok && has(why, "invalid GPG signature"), "altered signed file rejected: " + why);

    // public API
    CHECK(m.ping() == "ok", "ping");
    std::string st;
    m.setBinDir(W + "/fakebin");
    st = m.binStatus();
    CHECK(has(st, "\"xmrig\":{\"present\":true,\"verified\":false"), "unmanaged binary reports unverified: " + st);
    m.mineStart("wallet=" + std::string("4") + std::string(94, 'A') + "\nthreads=2\nnodes=127.0.0.1:" + argv[3] + ":" + argv[4]);
    for (int i = 0; i < 20 && has(m.mineState(), "\"running\":true"); ++i) usleep(200000);
    CHECK(has(m.mineState(), "does not match its recorded SHA256"), "start refused for unverified: " + m.mineState());
    CHECK(has(m.mineStart("wallet=short\nthreads=2\nnodes=a:1:2"), "wallet must be"), "bad wallet rejected");
    CHECK(has(m.mineStart("wallet=" + std::string("4") + std::string(94, 'A') + "\nthreads=0\nnodes=a:1:2"), "threads"), "bad threads rejected");
    CHECK(has(m.binTrust("xmrig"), "trusted") && has(m.binTrust("p2pool"), "trusted"), "binTrust");
    st = m.binStatus();
    CHECK(has(st, "\"xmrig\":{\"present\":true,\"verified\":true"), "trusted binary verified: " + st);

    std::string wallet = "4" + std::string(94, 'A');
    // first node dead port 1, second node good
    std::string r = m.mineStart("wallet=" + wallet + "\nthreads=3\nnodes=127.0.0.1:1:2,127.0.0.1:" + argv[3] + ":" + argv[4] + "\nsidechain=mini");
    CHECK(has(r, "started"), "mineStart ok: " + r);
    std::string state;
    for (int i = 0; i < 40; ++i) { usleep(250000); state = m.mineState(); if (has(state, "\"phase\":\"mining\"") && has(state, "1234.5")) break; }
    CHECK(has(state, "\"phase\":\"mining\""), "phase mining: " + state);
    CHECK(has(state, "\"hashrate\":\"1234.5\"") && has(state, "\"shares\":\"3/3\""), "hashrate/shares parsed");
    CHECK(has(state, "\"h60\":\"1200.0\"") && has(state, "\"h15\":\"\"") && has(state, "\"uptime\":"), "h60/h15/uptime in state: " + state);
    CHECK(has(state, "127.0.0.1:" + std::string(argv[3])), "failed over to second node");
    std::string lg = m.mineLog("0");
    CHECK(has(lg, "TCPServer listening") && has(lg, "working RPC and ZMQ") && has(lg, "[xmrig]"), "log has p2pool/xmrig/core lines");
    CHECK(!has(lg, "SideChain verified block"), "noisy sync lines filtered from the log");
    CHECK(has(rd(W + "/p2pool.args"), "--mini") && has(rd(W + "/p2pool.args"), "--stratum 127.0.0.1:3333"), "p2pool args: " + rd(W + "/p2pool.args"));
    CHECK(has(rd(W + "/xmrig.args"), "-o 127.0.0.1:3333") && has(rd(W + "/xmrig.args"), "-t 3"), "xmrig args: " + rd(W + "/xmrig.args"));
    CHECK(has(m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=a:1:2"), "already running"), "double start refused");
    CHECK(has(m.binInstall("xmrig"), "stop mining"), "install refused while mining");
    r = m.mineStop();
    CHECK(has(r, "stopping"), "mineStop " + r);
    for (int i = 0; i < 60; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    state = m.mineState();
    CHECK(has(state, "\"running\":false") && has(state, "\"phase\":\"idle\""), "stopped cleanly: " + state);
    system("sleep 0.5; pgrep -f 'fakebin/(xmrig|p2pool)' >/dev/null && echo LEFTOVER || echo none-left > " "/dev/null");
    system("echo '--- procs:'; pgrep -af '^/bin/bash [^ ]*/fakebin/' ; echo '--- end'");
    int left = system("pgrep -f '^/bin/bash [^ ]*/fakebin/' >/dev/null");
    CHECK(left != 0, "no child processes left");

    // node whose ZMQ port accepts TCP but never answers the ZMQ handshake -> rejected quickly
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.2:" + argv[5] + ":" + argv[5]);
    for (int i = 0; i < 60; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    CHECK(has(m.mineState(), "\"phase\":\"error\"") && has(m.mineLog("0"), "0 of 1 nodes have working RPC and ZMQ"), "TCP-only ZMQ port rejected by handshake probe");

    // p2pool dies right after XMRig starts (ZMQ failure) -> treated as node failure, everything stopped
    system(("touch " + W + "/p2pool.crash").c_str());
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.1:" + argv[3] + ":" + argv[4]);
    for (int i = 0; i < 80; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    lg = m.mineLog("0");
    CHECK((has(lg, "could not use this node's ZMQ") || has(lg, "died right after starting")) && has(m.mineState(), "\"running\":false"), "p2pool ZMQ crash -> node failure handled: " + m.mineState());
    CHECK(system("pgrep -f '^/bin/bash [^ ]*/fakebin/xmrig' >/dev/null") != 0, "xmrig stopped after p2pool crash");
    system(("rm -f " + W + "/p2pool.crash").c_str());

    // public node list: parsing + use as fallback
    { std::string first; size_t n = hushrateTestParseNodes(rd(W + "/srv/nodes.json"), first);
      CHECK(n == 1 && first == "127.0.0.1:" + std::string(argv[3]), "dynamic list parsed/filtered (tor, low uptime, testnet, unavailable dropped): n=" + std::to_string(n) + " " + first); }
    hushrateTestDynamic(api + "/nodes.json");
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\ndynamic=1\nnodes=127.0.0.1:1:2");
    for (int i = 0; i < 80; ++i) { usleep(250000); if (has(m.mineState(), "\"phase\":\"mining\"")) break; }
    CHECK(has(m.mineState(), "\"phase\":\"mining\"") && has(m.mineLog("0"), "public list: 1 candidate"), "public list used as fallback: " + m.mineState());
    m.mineStop();
    for (int i = 0; i < 60; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    hushrateTestDynamic(api + "/missing.json");
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.1:1:2");
    for (int i = 0; i < 40; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }

    // all nodes unusable -> error phase
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.1:1:2");
    for (int i = 0; i < 40; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    CHECK(has(m.mineState(), "\"phase\":\"error\"") && has(m.mineState(), "no usable node"), "no usable node -> error: " + m.mineState());

    // xmrig dies -> session ends, p2pool is stopped
    system(("touch " + W + "/xmrig.die").c_str());
    r = m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.1:" + argv[3] + ":" + argv[4]);
    for (int i = 0; i < 60; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
    CHECK(has(m.mineState(), "\"running\":false") && has(m.mineState(), "\"phase\":\"stopped\""), "xmrig exit ends session: " + m.mineState());
    CHECK(system("pgrep -f '^/bin/bash [^ ]*/fakebin/p2pool' >/dev/null") != 0, "p2pool stopped when xmrig died");

    // system / hardware / config / node scan
    { std::string a = m.sysStats(); usleep(300000); std::string b = m.sysStats();
      CHECK(has(b, "\"cpu\":") && has(b, "\"ram\":") && has(b, "\"temp\":") && !has(b, "\"ram\":-1"), "sysStats: " + b);
      std::string h = m.hwInfo();
      CHECK(has(h, "\"cores\":") && has(h, "\"recommended\":") && has(h, "\"hugeWant\":1280") && has(h, "\"l3kb\":") && !has(h, "\"cores\":0"), "hwInfo: " + h);
      CHECK(m.cfgLoad() == "\"\"", "cfgLoad empty when no file: " + m.cfgLoad());
      std::string txt = "wallet=" + wallet + "\nworker=rig\nthreads=3\nnote=\"q\" \\ ñ\n";
      CHECK(has(m.cfgSave(txt), "saved"), "cfgSave");
      std::string path = W + "/home/.local/share/hushrate/config.txt";
      CHECK(rd(path) == txt, "config file content exact");
      CHECK(system(("test \"$(stat -c %a " + path + ")\" = 600").c_str()) == 0, "config mode 0600");
      CHECK(has(m.cfgLoad(), "worker=rig") && has(m.cfgLoad(), "\\\"q\\\""), "cfgLoad returns JSON string");
      CHECK(has(m.cfgSave(std::string(70000, 'x')), "too large"), "oversized config rejected");
      hushrateTestDynamic(api + "/nodes.json");
      std::string sc = m.nodeScan("nodes=127.0.0.1:1:2\ndynamic=1");
      CHECK(has(sc, "started"), "nodeScan started: " + sc);
      std::string ns;
      for (int i = 0; i < 80; ++i) { usleep(250000); ns = m.nodeScanState(); if (has(ns, "\"running\":false")) break; }
      CHECK(has(ns, "\"valid\":true") && has(ns, "\"count\":1") && has(ns, "\"best\":{\"host\":\"127.0.0.1\""), "nodeScan result: " + ns);
      CHECK(has(m.nodeScan("nodes=\ndynamic=0"), "no nodes"), "nodeScan without sources refused");
    }


    // temperature limit pauses XMRig (SIGSTOP) and resumes it when it cools down
    { system(("rm -f " + W + "/xmrig.die").c_str());
      system(("echo 50 > " + W + "/faketemp; echo 0 > " + W + "/fakebat").c_str());
      setenv("HUSHRATE_FAKE_TEMP", (W + "/faketemp").c_str(), 1); setenv("HUSHRATE_FAKE_BAT", (W + "/fakebat").c_str(), 1);
      std::string rr = m.mineStart("wallet=" + wallet + "\nthreads=4\nmaxtemp=80\nsilent=1\npausebat=1\nnodes=127.0.0.1:" + argv[3] + ":" + argv[4]);
      std::string s2;
      for (int i = 0; i < 60; ++i) { usleep(250000); s2 = m.mineState(); if (has(s2, "\"phase\":\"mining\"")) break; }
      CHECK(has(rr, "started") && has(s2, "\"phase\":\"mining\""), "mining with safety options: " + rr + s2);
      CHECK(has(rd(W + "/xmrig.args"), "-t 2") && has(rd(W + "/xmrig.args"), "--cpu-priority 0"), "silent mode: half threads + low priority: " + rd(W + "/xmrig.args"));
      system(("echo 85 > " + W + "/faketemp").c_str());
      for (int i = 0; i < 40; ++i) { usleep(250000); s2 = m.mineState(); if (has(s2, "\"phase\":\"paused\"")) break; }
      CHECK(has(s2, "\"phase\":\"paused\"") && has(s2, "\"paused\":true") && has(s2, "limit 80"), "paused above the limit: " + s2);
      CHECK(system("pgrep -f '^/bin/bash [^ ]*/fakebin/xmrig' >/dev/null && ps -o stat= -p $(pgrep -f '^/bin/bash [^ ]*/fakebin/xmrig' | head -1) | grep -q T") == 0, "xmrig process is stopped (T)");
      system(("echo 75 > " + W + "/faketemp").c_str());
      for (int i = 0; i < 12; ++i) usleep(250000);
      CHECK(has(m.mineState(), "\"phase\":\"paused\""), "hysteresis: stays paused at 75 C");
      system(("echo 60 > " + W + "/faketemp").c_str());
      for (int i = 0; i < 40; ++i) { usleep(250000); s2 = m.mineState(); if (has(s2, "\"phase\":\"mining\"")) break; }
      CHECK(has(s2, "\"phase\":\"mining\"") && has(s2, "\"paused\":false"), "resumed after cooling: " + s2);
      system(("echo 1 > " + W + "/fakebat").c_str());
      for (int i = 0; i < 40; ++i) { usleep(250000); s2 = m.mineState(); if (has(s2, "\"phase\":\"paused\"")) break; }
      CHECK(has(s2, "\"phase\":\"paused\"") && has(s2, "battery"), "paused on battery: " + s2);
      m.mineStop();   // must work while paused (SIGCONT before SIGTERM)
      for (int i = 0; i < 60; ++i) { usleep(250000); if (has(m.mineState(), "\"running\":false")) break; }
      CHECK(has(m.mineState(), "\"running\":false") && system("pgrep -f '^/bin/bash [^ ]*/fakebin/' >/dev/null") != 0, "stop while paused kills everything");
      CHECK(has(m.mineStart("wallet=" + wallet + "\nthreads=1\nmaxtemp=20\nnodes=a:1:2"), "maxtemp"), "bad maxtemp rejected");
      unsetenv("HUSHRATE_FAKE_TEMP"); unsetenv("HUSHRATE_FAKE_BAT");
    }
    // update check (read only): installed tag older than the latest release -> newer
    { m.setBinDir(W + "/bin1");
      hushrateTestUpdate(api, "t/xmrig", "t/p2pool");
      system(("sed -i 's/v9.9.9/v1.0.0/' " + W + "/bin1/.hushrate-integrity.txt").c_str());
      m.updateCheck();
      std::string us;
      for (int i = 0; i < 40; ++i) { usleep(250000); us = m.updateState(); if (has(us, "\"running\":false") && has(us, "\"done\":true")) break; }
      CHECK(has(us, "\"xmrig\":{\"present\":true,\"cur\":\"v1.0.0\",\"latest\":\"v9.9.9\",\"newer\":true}"), "update available detected: " + us);
      CHECK(has(us, "\"p2pool\":{\"present\":true,\"cur\":\"v9.9\",\"latest\":\"v9.9\",\"newer\":false}"), "p2pool up to date: " + us);
      m.setBinDir(W + "/fakebin");
    }
    // parseNodes: host only, spaces/semicolons, http:// prefix
    { std::string first; hushrateTestParseNodes("{}", first); }
    { CHECK(has(m.nodeScan("nodes=127.0.0.1\ndynamic=0"), "started"), "node without ports accepted (defaults)");
      for (int i = 0; i < 80; ++i) { usleep(250000); if (has(m.nodeScanState(), "\"running\":false")) break; }
      std::string ns = m.nodeScanState();
      CHECK(has(ns, "\"probed\":1") && has(ns, "\"rpcFail\":1") && has(ns, "\"nodes\":[]"), "scan stats for an unreachable node: " + ns); }

    // leave one running on purpose: library destructor must kill it (clean exit)
    system(("rm -f " + W + "/xmrig.die").c_str());
    m.mineStart("wallet=" + wallet + "\nthreads=1\nnodes=127.0.0.1:" + argv[3] + ":" + argv[4]);
    for (int i = 0; i < 40; ++i) { usleep(250000); if (has(m.mineState(), "\"phase\":\"mining\"")) break; }
    std::cout << (fails ? "FAILURES: " : "ALL PASSED ") << fails << "\n";
    return fails ? 1 : 0;
}
