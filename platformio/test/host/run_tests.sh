#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .pio/host-tests
g++ -std=c++17 -Wall -Wextra -Werror -Itest/host/stubs/update_log -Iinclude \
  test/host/test_update_failure_log.cpp src/update_failure_log.cpp \
  -o .pio/host-tests/test_update_failure_log
.pio/host-tests/test_update_failure_log
g++ -std=c++17 -Wall -Wextra -Werror -Iinclude \
  test/host/test_busy_monitor.cpp -o .pio/host-tests/test_busy_monitor
.pio/host-tests/test_busy_monitor
g++ -std=c++17 -Wall -Wextra -Werror -Itest/host/stubs -Iinclude \
  test/host/test_driver_recovery.cpp src/epd/RobustGxEPD2_730c.cpp \
  -o .pio/host-tests/test_driver_recovery
.pio/host-tests/test_driver_recovery
