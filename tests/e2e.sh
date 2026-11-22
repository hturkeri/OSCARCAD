#!/usr/bin/env bash
# PersistCAD end-to-end check (headless).
#   tests/e2e.sh [BUILD_DIR] [--shots DIR] [--update-docs] [--no-harness]
# 1. builds an app from samples/bracket.pcad with pc_mkapp (standalone exe + .pcapp)
# 2. --validate-only on the embedded payload: passing set -> exit 0, failing set -> exit 2
#    (+ .pcapp path, JSON output, unknown parameter -> exit 1)
# 3. headless screenshots of the designer and the runner (ok + violation with ghost)
# 4. GUI harness (designer review flow, Save as App dialog, runner interactions) unless --no-harness
# --update-docs writes the final screenshots to docs/screenshots/final_*.png
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
SHOTS=""
UPDATE_DOCS=0
HARNESS=1
while [ $# -gt 0 ]; do
  case "$1" in
    --shots) SHOTS="$2"; shift 2 ;;
    --update-docs) UPDATE_DOCS=1; shift ;;
    --no-harness) HARNESS=0; shift ;;
    -h|--help) sed -n 2,10p "$0"; exit 0 ;;
    *) BUILD="$(cd "$1" && pwd)"; shift ;;
  esac
done
BIN="$BUILD/bin"
WORK="$(mktemp -d /tmp/pc_e2e.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
[ -n "$SHOTS" ] || SHOTS="$WORK/shots"
[ "$UPDATE_DOCS" = 1 ] && SHOTS="$ROOT/docs/screenshots"
mkdir -p "$SHOTS"
XVFB=(xvfb-run -a -s "-screen 0 1600x1000x24")
export QT_LOGGING_RULES="qt.qpa.*=false"
FAIL=0
pass() { echo "[ ok ] $*"; }
fail() { echo "[FAIL] $*"; FAIL=$((FAIL + 1)); }
expect_rc() {  # expect_rc WANT DESC cmd...
  local want="$1" desc="$2"; shift 2
  "$@" > "$WORK/out.txt" 2> "$WORK/err.txt"
  local rc=$?
  if [ "$rc" = "$want" ]; then pass "$desc (exit $rc)"; else fail "$desc: exit $rc, expected $want"; sed 's/^/    /' "$WORK/err.txt" | head -5; fi
}
png_ok() {  # png_ok FILE DESC : valid PNG, plausible size, not a blank image
  if python3 -I - "$1" <<'EOF'
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read()
assert d[:8] == b'\x89PNG\r\n\x1a\n'
w, h = struct.unpack('>II', d[16:24])
assert w >= 400 and h >= 300, (w, h)
assert len(d) > 20000, len(d)   # a blank/uniform window compresses to a few KB
EOF
  then pass "$2 ($(python3 -c "import struct;d=open('$1','rb').read();print('%dx%d'%struct.unpack('>II',d[16:24]))"))"; else fail "$2: $1 missing or not a plausible screenshot"; fi
}

for exe in pc_mkapp persistcad_runner persistcad; do
  [ -x "$BIN/$exe" ] || { echo "missing $BIN/$exe — build first: cmake -S . -B build -G Ninja && ninja -C build -j2"; exit 1; }
done

echo "== 1. build app from samples/bracket.pcad"
APP="$WORK/BraketApp"
expect_rc 0 "pc_mkapp builds standalone + .pcapp" "$BIN/pc_mkapp" "$ROOT/samples/bracket.pcad" "$BIN/persistcad_runner" "$APP" --name "Braket E2E" --version 1.2.3
[ -x "$APP" ] && pass "standalone executable exists" || fail "standalone executable missing"
[ -s "$APP.pcapp" ] && pass ".pcapp exists" || fail ".pcapp missing"
tail -c 16 "$APP" | grep -q PCAPP_PAYLOAD_V1 && pass "payload trailer present" || fail "payload trailer missing"

echo "== 2. --validate-only"
expect_rc 0 "embedded: defaults pass" "$APP" --validate-only
grep -q '"acceptable": true' "$WORK/out.txt" && pass "JSON says acceptable" || fail "JSON acceptable flag"
grep -q '"app": "Braket E2E"' "$WORK/out.txt" && pass "JSON carries embedded app name" || fail "embedded app name"
expect_rc 0 "embedded: passing set W=120 D=60 T=10" "$APP" --validate-only --set W=120 --set D=60 --set T=10
expect_rc 2 "embedded: failing set T=0.2 (min wall)" "$APP" --validate-only --set T=0.2
grep -q 'MIN_WALL' "$WORK/out.txt" && pass "failing report names MIN_WALL" || fail "MIN_WALL missing in report"
grep -q '"suggestedValue": 0.8' "$WORK/out.txt" && pass "suggestion T = 0.8 reported" || fail "suggestion for T missing"
expect_rc 2 "embedded: failing set R=1 Dbore=1.5 W=120 (hole L/D)" "$APP" --validate-only --text --set R=1 --set Dbore=1.5 --set W=120
grep -q 'UYGUN DEĞİL' "$WORK/out.txt" && pass "text report says UYGUN DEĞİL" || fail "text verdict"
expect_rc 0 ".pcapp via runner: defaults pass" "$BIN/persistcad_runner" "$APP.pcapp" --validate-only --text
expect_rc 2 ".pcapp via runner: Dh=0.5 fails" "$BIN/persistcad_runner" "$APP.pcapp" --validate-only --set Dh=0.5
expect_rc 1 "unknown parameter -> usage error" "$APP" --validate-only --set NOPE=1
sz=$(stat -c %s "$APP")
head -c $((sz - 5)) "$APP" > "$WORK/cut_trailer" && chmod +x "$WORK/cut_trailer"
expect_rc 1 "executable with a cut trailer -> no app, exit 1" "$WORK/cut_trailer" --validate-only
python3 -I - "$APP" "$WORK/bad_payload" <<'PYEOF'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[-24 - 40:-24 - 30] = b'#' * 10          # corrupt the payload JSON, keep size + magic
open(sys.argv[2], 'wb').write(d)
PYEOF
chmod +x "$WORK/bad_payload"
expect_rc 1 "executable with corrupted payload JSON -> load error, exit 1" "$WORK/bad_payload" --validate-only
grep -q 'Gömülü yük çözümlenemedi' "$WORK/err.txt" && pass "corrupted payload reported" || fail "corrupted payload message"

echo "== 3. headless screenshots"
"${XVFB[@]}" "$BIN/persistcad" "$ROOT/samples/bracket.pcad" --screenshot "$SHOTS/final_designer.png" > "$WORK/d.txt" 2>&1 \
  && png_ok "$SHOTS/final_designer.png" "designer screenshot" || fail "designer screenshot run"
expect_rc 0 "runner screenshot (defaults)" "${XVFB[@]}" "$APP" --screenshot "$SHOTS/final_runner_ok.png"
png_ok "$SHOTS/final_runner_ok.png" "runner ok screenshot"
expect_rc 0 "runner screenshot (violation + ghost)" "${XVFB[@]}" "$APP" --set R=1 --set Dbore=1.5 --set W=120 --screenshot "$SHOTS/final_runner_violation.png"
png_ok "$SHOTS/final_runner_violation.png" "runner violation screenshot"

if [ "$HARNESS" = 1 ]; then
  echo "== 4. GUI harness"
  if [ -x "$BIN/pc_gui_harness" ]; then
    "${XVFB[@]}" "$BIN/pc_gui_harness" --shots "$WORK/harness" > "$WORK/h.txt" 2>&1
    rc=$?
    grep -E '^\[(FAIL)\]' "$WORK/h.txt" | sed 's/^/    /'
    if [ $rc = 0 ]; then pass "GUI harness ($(grep -c '^\[ ok \]' "$WORK/h.txt") checks)"; else fail "GUI harness: $rc failure(s)"; fi
    for f in final_designer_review.png final_saveas_dialog.png; do
      [ -f "$WORK/harness/$f" ] && cp "$WORK/harness/$f" "$SHOTS/$f"
      png_ok "$SHOTS/$f" "harness screenshot $f"
    done
  else
    fail "pc_gui_harness not built (PC_BUILD_GUI=ON builds tests/gui)"
  fi
fi

echo
[ "$UPDATE_DOCS" = 1 ] || echo "screenshots: $SHOTS (temporary unless --shots/--update-docs)"
if [ $FAIL = 0 ]; then echo "E2E PASSED"; else echo "E2E FAILED: $FAIL check(s)"; fi
exit $FAIL
