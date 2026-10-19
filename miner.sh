#!/usr/bin/env bash
# Hushrate Miner for Logos Basecamp: build driver.
#
#   ./miner.sh prepare   build the module and the UI packages (.lgx) from this repository
#   ./miner.sh run       open the Basecamp dev build with the packages installed in an isolated profile
#   ./miner.sh package   collect the portable .lgx packages and their checksums in <build dir>/dist
#   ./miner.sh orphans   list xmrig/p2pool processes that are still alive
#   ./miner.sh reset     delete the isolated Basecamp dev profile (the official Basecamp is untouched)
#
# Needs: nix (flakes enabled), git, python3. Build files go to $HUSHRATE_BUILD (default ~/basecamp-miner).
# Optional: <build dir>/links.json  {"urlGithub": "https://...", "urlX": "https://..."}  (empty = button hidden)
set -euo pipefail
trap 'echo; echo "FAILED at line $LINENO. Copy the whole output above when reporting it."' ERR

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${HUSHRATE_BUILD:-$HOME/basecamp-miner}"
MODULE="$ROOT/module"
UI="$ROOT/ui"
RUN="$ROOT/run"
DIST="$ROOT/dist"
VERSION="$(tr -d '[:space:]' < "$REPO/VERSION")"
PKG_VERSION="0.9.0"   # package metadata must be semver; the UI shows $VERSION

track_changes() {   # nix flakes only see git-tracked files: stage what changed, file by file
  git ls-files -z --others --modified --exclude-standard | xargs -0 -r git add --
}

write_module() {
  mkdir -p "$MODULE/src"
  cp -f "$REPO"/module/src/minimal_impl.h "$REPO"/module/src/minimal_impl.cpp "$REPO"/module/src/hushrate_keys.h "$MODULE/src/"
  PKG_VERSION="$PKG_VERSION" python3 -I - "$MODULE/metadata.json" <<'PY'
import json, os, sys
p = sys.argv[1]
d = json.load(open(p))
d["version"] = os.environ["PKG_VERSION"]
json.dump(d, open(p, "w"), indent=2)
PY
}

write_ui() {
  mkdir -p "$UI/icons"
  cat > "$UI/flake.nix" <<NIX
{
  description = "Hushrate Miner UI";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # Attribute name must match the dependency name in metadata.json.
    minimal.url = "path:$MODULE";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
NIX
  local links=()
  [ -f "$ROOT/links.json" ] && links=("$ROOT/links.json")
  python3 -I "$REPO/tools/build_ui.py" "$UI/Main.qml" "${links[@]}"
  cp -f "$REPO/assets/sidebar_256.png" "$UI/icons/hushrate.png"
  PKG_VERSION="$PKG_VERSION" python3 -I - "$UI/metadata.json" <<'PY'
import json, os, sys
p = sys.argv[1]
try:
    d = json.load(open(p))
except Exception:
    d = {}
d.update({
    "name": "hushrate_miner",
    "version": os.environ["PKG_VERSION"],
    "description": "Hushrate Miner: solo Monero mining with P2Pool + XMRig",
    "type": "ui_qml",
    "view": "Main.qml",
    "dependencies": ["minimal"],
    "category": "tools",
    "icon": "icons/hushrate.png",
})
for k, v in list(d.items()):
    if isinstance(v, str) and "QML UI Example" in v:
        d[k] = v.replace("QML UI Example", "HushRate Miner")
d.pop("main", None)   # QML only: no C++ backend
json.dump(d, open(p, "w"), indent=2)
PY
}

build_both() {   # $1 = link name (module|ui)
  nix build '.#lgx' --out-link "$RUN/$1-lgx"
  if nix build '.#lgx-portable' --out-link "$RUN/$1-lgx-portable"; then
    echo "   (portable OK)"
  else
    echo "   WARNING: could not build .#lgx-portable for $1. The dev Basecamp still works."
  fi
}

prepare() {
  mkdir -p "$MODULE" "$UI" "$RUN"

  echo "== 1/4 Module (backend) =="
  cd "$MODULE"
  [ -f flake.nix ] || nix flake init -t github:logos-co/logos-module-builder
  [ -f metadata.json ] || { echo "The template did not create metadata.json. Check: ls -R $MODULE"; exit 1; }
  write_module
  [ -d .git ] || git init -q
  track_changes
  nix flake update
  git add flake.lock
  build_both module

  echo "== 2/4 Interface (QML) =="
  cd "$UI"
  [ -f flake.nix ] || nix flake init -t 'github:logos-co/logos-module-builder#ui-qml'
  write_ui
  [ -d .git ] || git init -q
  track_changes
  nix flake update
  track_changes
  build_both ui

  echo "== 3/4 Generated packages =="
  ls -l "$RUN"/*-lgx*/*.lgx
  echo "== 4/4 Done (v$VERSION). Next: ./miner.sh run  (dev Basecamp)  or  ./miner.sh package =="
}

run() {
  cd "$RUN"
  ls "$RUN"/module-lgx/*.lgx "$RUN"/ui-lgx/*.lgx >/dev/null   # fails if 'prepare' was not run
  echo "== 1/3 Basecamp and package manager (slow the first time) =="
  nix build 'github:logos-co/logos-basecamp' -o basecamp-result
  nix build 'github:logos-co/logos-package-manager#cli' --out-link ./pm
  echo "== 2/3 Installing into an isolated profile =="
  mkdir -p data/modules data/plugins
  ./pm/bin/lgpm --modules-dir data/modules install --file "$RUN"/module-lgx/*.lgx
  ./pm/bin/lgpm --ui-plugins-dir data/plugins install --file "$RUN"/ui-lgx/*.lgx
  echo "== 3/3 Opening Basecamp: open 'hushrate_miner' in the sidebar =="
  ./basecamp-result/bin/LogosBasecamp --user-dir "$RUN/data"
}

package() {
  ls "$RUN"/module-lgx-portable/*.lgx "$RUN"/ui-lgx-portable/*.lgx >/dev/null \
    || { echo "Portable packages are missing: run ./miner.sh prepare first."; exit 1; }
  mkdir -p "$DIST"
  cp -f "$RUN"/module-lgx-portable/*.lgx "$DIST/hushrate-core-$VERSION.lgx"
  cp -f "$RUN"/ui-lgx-portable/*.lgx "$DIST/hushrate-miner-ui-$VERSION.lgx"
  ( cd "$DIST" && sha256sum ./*.lgx > SHA256SUMS && cat SHA256SUMS )
  echo
  echo "Packages are in: $DIST"
  echo "Install order in Basecamp: the core package first, then the UI package."
}

orphans() {
  echo "Live xmrig/p2pool processes (should be 'none' after Basecamp is closed):"
  pgrep -a -x 'xmrig|p2pool' || echo "  none"
}

reset() {
  rm -rf "$RUN/data"
  echo "Isolated profile deleted: $RUN/data"
}

case "${1:-}" in
  prepare) prepare ;;
  run) run ;;
  package) package ;;
  orphans) orphans ;;
  reset) reset ;;
  *) sed -n '2,11p' "$0"; exit 1 ;;
esac
