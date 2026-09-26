#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/sjz-tests
mkdir -p "$OUT"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_collector_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_collector_test"
"$OUT/sjz_collector_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_aim_test.cpp lara/kexploit/sjz/SJZAim.cpp \
    lara/kexploit/sjz/OwnGameData.cpp lara/kexploit/sjz/OwnProjection.cpp \
    -o "$OUT/sjz_aim_test"
"$OUT/sjz_aim_test"
printf 'PASS: aim trigger, part, speed, three-axis prediction and LOS fixture\n'
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit tests/sjz_touch_aim_policy_test.cpp -o "$OUT/sjz_touch_aim_policy_test"
"$OUT/sjz_touch_aim_policy_test"
printf 'PASS: touch aim HID and camera feedback policy fixture\n'
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Ilara/kexploit \
    tests/sjz_pending_touch_queue_test.cpp -o "$OUT/sjz_pending_touch_queue_test"
"$OUT/sjz_pending_touch_queue_test"
printf 'PASS: pending touch lifecycle\n'
for test in sjzmem_partial sjz_hud_lifecycle_policy sjz_remote_call_cleanup_state; do
    "${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Ilara/kexploit \
        "tests/${test}_test.c" -o "$OUT/${test}_test"
    "$OUT/${test}_test"
    printf 'PASS: %s\n' "$test"
done
