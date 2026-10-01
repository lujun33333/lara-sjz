#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/sjz-tests
mkdir -p "$OUT"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit tests/sjz_menu_policy_test.cpp -o "$OUT/sjz_menu_policy_test"
"$OUT/sjz_menu_policy_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_cn_skeleton_test.cpp \
    lara/kexploit/sjz/OwnGameData.cpp lara/kexploit/sjz/OwnProjection.cpp \
    -o "$OUT/sjz_cn_skeleton_test"
"$OUT/sjz_cn_skeleton_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_visibility_test.cpp \
    -o "$OUT/sjz_visibility_test"
"$OUT/sjz_visibility_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_player_kind_test.cpp \
    -o "$OUT/sjz_player_kind_test"
"$OUT/sjz_player_kind_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit tests/sjz_weapon_catalog_test.cpp -o "$OUT/sjz_weapon_catalog_test"
"$OUT/sjz_weapon_catalog_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_armor_test.cpp \
    lara/kexploit/sjz/OwnArmor.cpp -o "$OUT/sjz_armor_test"
"$OUT/sjz_armor_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_cn_aim_math_test.cpp \
    lara/kexploit/sjz/CnAimMath.cpp -o "$OUT/sjz_cn_aim_math_test"
"$OUT/sjz_cn_aim_math_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_angle_write_test.cpp \
    lara/kexploit/sjz/OwnGameData.cpp lara/kexploit/sjz/OwnProjection.cpp \
    -o "$OUT/sjz_angle_write_test"
"$OUT/sjz_angle_write_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_collector_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_collector_test"
"$OUT/sjz_collector_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_cn_alignment_collector_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_cn_alignment_collector_test"
"$OUT/sjz_cn_alignment_collector_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_collector_read_budget_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_collector_read_budget_test"
"$OUT/sjz_collector_read_budget_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_frame_pipeline_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_frame_pipeline_test"
"$OUT/sjz_frame_pipeline_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_collector_live_test.cpp lara/kexploit/sjz/*.cpp -x c++ lara/kexploit/sjzesp.mm \
    -o "$OUT/sjz_collector_live_test"
"$OUT/sjz_collector_live_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/third_party/imgui tests/sjz_bulk_reads_test.cpp \
    lara/kexploit/sjz/OwnGameData.cpp lara/kexploit/sjz/OwnProjection.cpp \
    -o "$OUT/sjz_bulk_reads_test"
"$OUT/sjz_bulk_reads_test"
"$CXX" -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Ilara/kexploit -Ilara/kexploit/sjz -Ilara/third_party/imgui \
    tests/sjz_aim_test.cpp lara/kexploit/sjz/SJZAim.cpp \
    lara/kexploit/sjz/OwnGameData.cpp lara/kexploit/sjz/OwnProjection.cpp lara/kexploit/sjz/CnAimMath.cpp \
    -o "$OUT/sjz_aim_test"
"$OUT/sjz_aim_test"
printf 'PASS: cn head/chest/legs, trigger, zero endpoints, prediction and LOS fixture\n'
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Ilara/kexploit \
    tests/sjz_pending_touch_queue_test.cpp -o "$OUT/sjz_pending_touch_queue_test"
"$OUT/sjz_pending_touch_queue_test"
printf 'PASS: pending touch lifecycle\n'
for test in sjzmem_partial sjz_hud_lifecycle_policy sjz_remote_call_cleanup_state sjz_page_cache_policy sjz_source_age_policy sjz_read_phase; do
    "${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -Ilara/kexploit \
        "tests/${test}_test.c" -lm -o "$OUT/${test}_test"
    "$OUT/${test}_test"
    printf 'PASS: %s\n' "$test"
done
