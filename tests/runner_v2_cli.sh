#!/usr/bin/env bash
# Runner v2 CLI check: usage  tests/runner_v2_cli.sh [build-dir]   (default build-e2)
set -euo pipefail
B=${1:-build-e2}; D=$(mktemp -d); trap 'rm -rf "$D"' EXIT
$B/bin/pc_mkapp samples/v2_multibody.pcad $B/bin/persistcad_runner "$D/a" --no-exe 2>/dev/null || true
R=$B/bin/persistcad_runner
python3 -I - "$($R "$D/a.pcapp" --validate-only || true)" <<'PY'
import json, sys
j = json.loads(sys.argv[1])
assert len(j["bodies"]) >= 2, "bodies missing"
assert all({"name", "volumeMm3", "massG"} <= set(b) for b in j["bodies"])
names = {b["name"] for b in j["bodies"]}
assert any(i.get("body") in names for i in j["issues"]), "no issue carries a body name"
print("json ok:", sorted(names))
PY
$R "$D/a.pcapp" --validate-only --text | grep -q "Gövdeler (" && $R "$D/a.pcapp" --validate-only --text | grep -q "\[Gövde: "
echo "text ok"
