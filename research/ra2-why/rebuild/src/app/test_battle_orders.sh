#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d /tmp/ra2-battle-orders.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror ${BATTLE_ORDERS_TEST_FLAGS:-} \
    -I .. battle_orders.cpp test_battle_orders.cpp -o "$build/test_battle_orders"
"$build/test_battle_orders"
