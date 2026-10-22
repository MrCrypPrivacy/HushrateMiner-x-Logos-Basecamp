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
# Link buttons: ui/links.json {"urlGithub": "https://...", "urlX": "https://..."} (empty = button hidden); <build dir>/links.json overrides it
set -euo pipefail
trap 'echo; echo "FAILED at line $LINENO. Copy the whole output above when reporting it."' ERR

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${HUSHRATE_BUILD:-$HOME/basecamp-miner}"
MODULE="$ROOT/module"
UI="$ROOT/ui"
RUN="$ROOT/run"
DIST="$ROOT/dist"
VERSION="$(tr -d '[:space:]' < "$REPO/VERSION")"

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }

track_changes() {   # nix flakes only see git-tracked files: stage what changed, file by file
  git ls-files -z --others --modified --exclude-standard | xargs -0 -r git add --
}

check_versions() {   # the package version (semver) lives in both metadata.json files and must match
  python3 -I - "$REPO/module/metadata.json" "$REPO/ui/metadata.json" <<'PY'
import json, sys
v = [json.load(open(p))["version"] for p in sys.argv[1:]]
if v[0] != v[1]:
    sys.exit("module/metadata.json (%s) and ui/metadata.json (%s) must have the same version" % (v[0], v[1]))
PY
}

write_module() {
  mkdir -p "$MODULE/src"
  rm -f "$MODULE"/src/minimal_impl.h "$MODULE"/src/minimal_impl.cpp   # left over from the template
  cp -f "$REPO"/module/src/hushrate_core_impl.h "$REPO"/module/src/hushrate_core_impl.cpp "$REPO"/module/src/hushrate_keys.h "$MODULE/src/"
  cp -f "$REPO"/module/flake.nix "$REPO"/module/metadata.json "$REPO"/module/CMakeLists.txt "$MODULE/"
}

write_ui() {
  mkdir -p "$UI/icons"
  cat > "$UI/flake.nix" <<NIX
{
  description = "Hushrate Miner UI";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # Attribute name must match the dependency name in metadata.json.
    hushrate_core.url = "path:$MODULE";
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
  if [ -f "$ROOT/links.json" ]; then links=("$ROOT/links.json")
  elif [ -f "$REPO/ui/links.json" ]; then links=("$REPO/ui/links.json"); fi
  python3 -I "$REPO/tools/build_ui.py" "$UI/Main.qml" ${links[@]+"${links[@]}"}
  cp -f "$REPO/assets/sidebar_256.png" "$UI/icons/hushrate.png"
  cp -f "$REPO/ui/metadata.json" "$UI/metadata.json"
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
  check_versions
  mkdir -p "$MODULE" "$UI" "$RUN"

  echo "== 1/4 Module (backend) =="
  cd "$MODULE"
  write_module
  [ -d .git ] || git init -q
  track_changes
  nix flake update
  git add flake.lock
  build_both module

  echo "== 2/4 Interface (QML) =="
  cd "$UI"
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
  ( cd "$DIST" && sha256 ./*.lgx > SHA256SUMS && cat SHA256SUMS )
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
