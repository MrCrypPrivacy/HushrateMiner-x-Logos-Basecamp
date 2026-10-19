#pragma once

#include <string>
#include "logos_module_context.h"

// Hushrate Miner core (Linux): downloads and verifies XMRig/P2Pool (SHA256 +
// pinned GPG key), runs P2Pool + XMRig, exposes state. Everything is
// std::string; replies are JSON text.
class MinimalImpl : public LogosModuleContext
{
public:
    std::string ping();

    // --- binaries ---
    /// Sets the folder holding xmrig/p2pool ("" = default ~/.local/share/hushrate/bin).
    std::string setBinDir(const std::string& dir);
    /// JSON: tools (curl/gpg/tar), and per binary: present, verified, tag, version.
    std::string binStatus();
    /// Starts a background download+verify of "xmrig", "p2pool" or "all".
    std::string binInstall(const std::string& which);
    /// Marks an existing binary as trusted (records its current SHA256).
    std::string binTrust(const std::string& which);
    /// JSON: {"running":bool,"ok":bool,"log":[...]} of the last install job.
    std::string jobState();

    // --- system / config ---
    /// JSON {"cpu":%,"ram":%,"temp":C} (-1 when unknown).
    std::string sysStats();
    /// JSON {"cores","logical","ramGb","hugePages","msr","recommended"}.
    std::string hwInfo();
    /// Persistent settings text (~/.local/share/hushrate/config.txt, mode 0600).
    std::string cfgLoad();
    std::string cfgSave(const std::string& text);
    /// Background probe of the candidate nodes (same cfg keys as mineStart: nodes, dynamic).
    std::string nodeScan(const std::string& cfg);
    /// JSON {"running","valid","count","best":{host,rpc,zmq,ms}}.
    std::string nodeScanState();

    /// Background lookup of the latest XMRig/P2Pool releases (read only).
    std::string updateCheck();
    /// JSON {"running","done","error","xmrig":{"present","cur","latest","newer"},"p2pool":{...}}.
    std::string updateState();

    // --- mining ---
    /// cfg = lines "key=value": wallet, threads, nodes (host:rpc:zmq,...), dynamic (0/1: also use the public node list), light (0/1), sidechain (main/mini/nano), maxtemp (0=off, 50-100 C: pause above it), silent (0/1: half the threads, lowest priority), pausebat (0/1: pause on battery).
    std::string mineStart(const std::string& cfg);
    std::string mineStop();
    /// JSON: phase (…/mining/paused), node, hashrate (10 s), h60, h15, shares, p2poolShares, error, uptime (s), paused, pauseReason.
    std::string mineState();
    /// JSON: {"next":N,"lines":[...]} with log lines since index <since>.
    std::string mineLog(const std::string& since);
};
