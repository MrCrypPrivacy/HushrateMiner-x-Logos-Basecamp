#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
W=${1:?work dir}; rm -rf "$W"; mkdir -p "$W/files" "$W/srv/repos" "$W/gnupg" "$W/fakebin" "$W/home"
chmod 700 "$W/gnupg"
export GNUPGHOME="$W/gnupg"
gpg --batch --passphrase '' --quick-generate-key "hushrate test <t@t>" ed25519 sign never 2>/dev/null
gpg --list-keys --with-colons | awk -F: '/^fpr/ {print $10; exit}' > "$W/fpr.txt"
gpg --armor --export > "$W/pub.asc"
head -c 100000 /dev/urandom > "$W/files/data.bin"; sha256sum "$W/files/data.bin" | cut -d' ' -f1 | tr -d '\n' > "$W/data.sha"
PORT=$((20000 + RANDOM % 20000)); RPC=$((PORT+1)); ZMQ=$((PORT+2))
mk() { # repo bin asset mode [tamper]
  local repo=$1 bin=$2 asset=$3 mode=$4 tamper=${5:-0}
  local F="$W/files/$repo"; mkdir -p "$F"
  local d=$(mktemp -d); mkdir -p "$d/${bin}-9.9.9"; printf '#!/bin/sh\necho %s 9.9.9\n' "$bin" > "$d/${bin}-9.9.9/$bin"; chmod +x "$d/${bin}-9.9.9/$bin"
  tar -czf "$F/$asset" -C "$d" "${bin}-9.9.9"
  local h=$(sha256sum "$F/$asset" | cut -d' ' -f1)
  if [ "$mode" = detached ]; then
    echo "$h *$asset" > "$F/SHA256SUMS"; gpg --batch --yes --detach-sign -o "$F/SHA256SUMS.sig" "$F/SHA256SUMS"
  else
    printf 'Name: %s\nSHA256: %s\nSize: 1\n' "$asset" "$h" > "$d/s"; gpg --batch --yes --clearsign -o "$F/sha256sums.txt.asc" "$d/s"
  fi
  [ "$tamper" = 1 ] && echo tampered >> "$F/$asset"
  [ "$tamper" = 2 ] && echo tampered >> "$F/SHA256SUMS"
  local rel="$W/srv/repos/t/$repo/releases"; mkdir -p "$rel"
  local base="http://127.0.0.1:$PORT/files/$repo"
  if [ "$mode" = detached ]; then
    printf '{"tag_name":"v9.9.9","assets":[{"name":"%s","browser_download_url":"%s/%s"},{"name":"SHA256SUMS","browser_download_url":"%s/SHA256SUMS"},{"name":"SHA256SUMS.sig","browser_download_url":"%s/SHA256SUMS.sig"}]}' "$asset" "$base" "$asset" "$base" "$base" > "$rel/latest"
  else
    printf '{"tag_name":"v9.9","assets":[{"name":"%s","browser_download_url":"%s/%s"},{"name":"sha256sums.txt.asc","browser_download_url":"%s/sha256sums.txt.asc"}]}' "$asset" "$base" "$asset" "$base" > "$rel/latest"
  fi
}
mk xmrig xmrig xmrig-9.9.9-linux-static-x64.tar.gz detached
mk p2pool p2pool p2pool-v9.9-linux-x64.tar.gz clear
mk bad xmrig xmrig-9.9.9-linux-static-x64.tar.gz detached 1
mk badsig xmrig xmrig-9.9.9-linux-static-x64.tar.gz detached 2
cat > "$W/srv/nodes.json" <<JSN
{"data":{"items":[
 {"hostname":"127.0.0.1","port":$RPC,"nettype":"mainnet","is_available":true,"is_tor":false,"is_i2p":false,"uptime":99.5},
 {"hostname":"tor.example","port":18081,"nettype":"mainnet","is_available":true,"is_tor":true,"is_i2p":false,"uptime":99},
 {"hostname":"low.example","port":18081,"nettype":"mainnet","is_available":true,"is_tor":false,"is_i2p":false,"uptime":50},
 {"hostname":"testnet.example","port":28081,"nettype":"testnet","is_available":true,"is_tor":false,"is_i2p":false,"uptime":99},
 {"hostname":"down.example","port":18081,"nettype":"mainnet","is_available":false,"is_tor":false,"is_i2p":false,"uptime":99}
]}}
JSN
( cd "$W/srv" && nohup python3 -m http.server "$PORT" --bind 127.0.0.1 >/dev/null 2>&1 < /dev/null & echo $! > "$W/http.pid" )
# serve /files from the same server
ln -sfn "$W/files" "$W/srv/files"
cat > "$W/lis.py" <<'PYL'
import socket,threading,time,sys
rpc,zmq,dumb=int(sys.argv[1]),int(sys.argv[2]),int(sys.argv[3])
def serve(p,hs,host='127.0.0.1'):
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind((host,p)); s.listen(50)
    def loop():
        while True:
            c,_=s.accept()
            if hs:
                try: c.recv(10); c.sendall(b'\xff\x00\x00\x00\x00\x00\x00\x00\x01\x7f')
                except Exception: pass
            else:
                pass   # accepts TCP, never speaks ZMQ (like a firewall/proxy)
    threading.Thread(target=loop,daemon=True).start()
serve(rpc,False); serve(zmq,True); serve(dumb,False,'127.0.0.2')
try: serve(18083,True)
except Exception: pass
time.sleep(600)
PYL
nohup python3 "$W/lis.py" $RPC $ZMQ $((PORT+3)) >/dev/null 2>&1 < /dev/null &
echo $! > "$W/lis.pid"
cat > "$W/fakebin/p2pool" <<EOS
#!/bin/bash
echo "\$@" > $W/p2pool.args
trap 'echo "P2Pool stopping"; exit 0' TERM
echo "NOTICE  P2Pool v4 fake"
sleep 0.5
echo "NOTICE  TCPServer listening on 0.0.0.0:37888"
echo "SideChain verified block at height = 1, depth = 0"
echo "NOTICE  TCPServer listening on 127.0.0.1:3333"
if [ -f $W/p2pool.crash ]; then sleep 1; echo "ZMQReader failed to connect to tcp://x:18084"; echo "P2Pool can't continue execution: panic"; exit 134; fi
while true; do sleep 0.2; done
EOS
cat > "$W/fakebin/xmrig" <<EOS
#!/bin/bash
echo "\$@" > $W/xmrig.args
trap 'echo "signal SIGTERM received, exiting"; exit 0' TERM
n=0
while true; do
  echo "[2026] miner  speed 10s/60s/15m 1234.5 1200.0 n/a H/s max 1300.0 H/s"
  echo "[2026] cpu    accepted (3/3) diff 100000"
  n=\$((n+1)); [ -f $W/xmrig.die ] && [ \$n -ge 3 ] && exit 3
  sleep 0.5
done
EOS
chmod +x "$W/fakebin/"*
sleep 1
export HOME="$W/home"
RC=0
g++ -std=c++17 -DHUSHRATE_TEST -I"$HERE/stub" -I"$HERE/../module/src" "$HERE/../module/src/hushrate_core_impl.cpp" "$HERE/test_main.cpp" -o "$W/t" -pthread
stdbuf -oL "$W/t" "$W" "$PORT" "$RPC" "$ZMQ" "$((PORT+3))" || RC=$?
kill "$(cat "$W/http.pid")" "$(cat "$W/lis.pid")" 2>/dev/null || true
echo "leftover fake procs after exit:"; pgrep -af "$W/fakebin" || echo "  none"
exit $RC
