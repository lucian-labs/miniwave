#!/bin/bash
# miniwave deploy to Pi — build, sync, rebuild, restart, validate
set -e

PI="waveloop@pocketwave.local"
PI_HOST="pocketwave.local"
REMOTE_DIR="~/miniwave"
LOCAL_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== miniwave deploy ==="

# 1. Lint JS
echo "[1/8] Linting JS..."
node "$LOCAL_DIR/common/web/lint.js"

# 2. Build locally
echo "[2/8] Building locally..."
cd "$LOCAL_DIR/linux"
make -q 2>/dev/null || make
LOCAL_VER=$(grep 'MINIWAVE_VERSION' "$LOCAL_DIR/common/rack.h" | head -1 | grep -o '"[^"]*"' | tr -d '"')
echo "       local version: $LOCAL_VER"

# 3. Stop miniwave
echo "[3/8] Stopping miniwave..."
ssh "$PI" "sudo systemctl stop miniwave 2>/dev/null || pkill miniwave 2>/dev/null" || true
sleep 1

# 4. Clean remote web + binary, then sync
echo "[4/8] Syncing to $PI (clean)..."
ssh "$PI" "rm -rf $REMOTE_DIR/common/web $REMOTE_DIR/linux/miniwave"
rsync -az --delete \
  --exclude='.git' \
  --exclude='linux/miniwave' \
  --exclude='macos/miniwave' \
  --exclude='.claude' \
  "$LOCAL_DIR/" "$PI:$REMOTE_DIR/"

# 4. Verify source arrived
REMOTE_VER=$(ssh "$PI" "grep 'MINIWAVE_VERSION' $REMOTE_DIR/common/rack.h | head -1 | grep -o '\"[^\"]*\"' | tr -d '\"'")
if [ "$LOCAL_VER" != "$REMOTE_VER" ]; then
  echo "FAIL: version mismatch after sync (local=$LOCAL_VER remote=$REMOTE_VER)"
  exit 1
fi
echo "       source verified: $REMOTE_VER"

# 5. Build on Pi
echo "[5/8] Building on Pi..."
ssh "$PI" "cd $REMOTE_DIR/linux && rm -f miniwave && make 2>&1 | tail -1"
BIN_VER=$(ssh "$PI" "strings $REMOTE_DIR/linux/miniwave | grep -oP '^\d+\.\d+\.\d+$' | head -1")
if [ "$LOCAL_VER" != "$BIN_VER" ]; then
  echo "FAIL: binary version mismatch (expected=$LOCAL_VER got=$BIN_VER)"
  exit 1
fi
echo "       binary verified: $BIN_VER"

# 6. Start via systemd
echo "[6/8] Starting miniwave..."
ssh "$PI" "sudo systemctl start miniwave"
sleep 2

# 7. Validate — hit the API from localhost on Pi
echo "[7/8] Validating API (localhost)..."
LIVE_VER=$(ssh "$PI" "curl -sf http://localhost:8080/api -d '{\"type\":\"rack_status\"}' | grep -o 'version\":\"[^\"]*' | cut -d'\"' -f3")
if [ "$LOCAL_VER" != "$LIVE_VER" ]; then
  echo "FAIL: API version mismatch (expected=$LOCAL_VER got=$LIVE_VER)"
  echo "       check: ssh $PI tail -30 /tmp/miniwave.log"
  exit 1
fi
echo "       API ok: v$LIVE_VER"

# 8. Validate — hit the API from this machine (not the Pi)
echo "[8/8] Validating remote API (http://$PI_HOST:8080)..."
REMOTE_LIVE=$(curl -sf "http://$PI_HOST:8080/api" -d '{"type":"rack_status"}' | grep -o 'version":"[^"]*' | cut -d'"' -f3)
if [ "$LOCAL_VER" != "$REMOTE_LIVE" ]; then
  echo "FAIL: remote API version mismatch (expected=$LOCAL_VER got=$REMOTE_LIVE)"
  exit 1
fi
echo "       remote API ok: v$REMOTE_LIVE"

echo ""
echo "=== deployed miniwave v$LIVE_VER ==="
echo "    http://$PI_HOST:8080"
