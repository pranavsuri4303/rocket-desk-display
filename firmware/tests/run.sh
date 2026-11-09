#!/bin/sh
set -eu
: "${IDF_PATH:?Set IDF_PATH to ESP-IDF 5.5.2}"
tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fw_dir=$(dirname "$tests_dir")
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/glance-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT
python3 "$fw_dir/tools/generate_slides.py" "$tests_dir/slides.json" "$build_dir/glance_config.h"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$build_dir" -I"$fw_dir/main/core" -I"$IDF_PATH/components/json/cJSON" \
  "$tests_dir/test_core.c" "$fw_dir/main/core/core.c" "$fw_dir/main/core/ui.c" \
  "$fw_dir/main/core/settings.c" "$fw_dir/main/features/transit.c" "$fw_dir/main/features/slides.c" \
  "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$build_dir/test_core"
"$build_dir/test_core"
