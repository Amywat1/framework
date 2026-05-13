#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BINARY="$REPO_ROOT/build/src/wash_controller"

if [ ! -x "$BINARY" ]; then
    echo "missing controller binary: $BINARY" >&2
    echo "run: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

OUTPUT=$(cd "$REPO_ROOT" && printf 'homing\nstart wash_step_control_v1\nstatus\n' | "$BINARY")
printf '%s\n' "$OUTPUT"

printf '%s\n' "$OUTPUT" | grep -q "result=running accepted=true"
printf '%s\n' "$OUTPUT" | grep -q "stage=roof_segment"
printf '%s\n' "$OUTPUT" | grep -q "lifecycle=running"

echo "hil smoke precheck passed: controller can load wash_step_control_v1 and enter roof_segment"
