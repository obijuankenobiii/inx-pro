#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
clock_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/inx-clock-sync.XXXXXX")
trap 'rm -rf "$clock_test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Itest/clock_sync/include -Isrc test/clock_sync/ClockSyncTest.cpp src/system/NtpTimeSync.cpp \
  -o "$clock_test_dir/clock-sync-test"
"$clock_test_dir/clock-sync-test"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-unused-variable -fsanitize=address,undefined \
  -DFREEINK_DEVICE_PICCO=1 -DFREEINK_CAP_RTC=1 \
  -Isrc -Ifreeink-sdk/libs/hardware/Rtc/test/host/stubs \
  -Ifreeink-sdk/libs/hardware/BatteryMonitor/test/host/stubs \
  -Ifreeink-sdk/libs/hardware/Rtc/include -Ifreeink-sdk/libs/hardware/BoardConfig/include \
  test/clock_sync/PiccoRtcTest.cpp freeink-sdk/libs/hardware/Rtc/src/Rtc.cpp \
  -o "$clock_test_dir/picco-rtc-test"
"$clock_test_dir/picco-rtc-test"
