#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
power_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/inx-picco-power.XXXXXX")
trap 'rm -rf "$power_test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Ilib/hal_picco -Ifreeink-sdk/libs/hardware/BatteryMonitor/include \
  test/picco_battery/PiccoBatteryStateTest.cpp -o "$power_test_dir/state"
"$power_test_dir/state"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DFREEINK_DEVICE_PICCO=1 \
  -Ilib/hal_picco -Ifreeink-sdk/libs/hardware/BatteryMonitor/include \
  -Ifreeink-sdk/libs/hardware/BatteryMonitor/test/host/stubs \
  -Ifreeink-sdk/libs/hardware/BoardConfig/include \
  -Ifreeink-sdk/libs/hardware/XteinkDetect/test/host/stubs \
  test/picco_battery/PiccoChargerTest.cpp freeink-sdk/libs/hardware/BatteryMonitor/src/BatteryMonitor.cpp \
  -o "$power_test_dir/charger"
"$power_test_dir/charger"
