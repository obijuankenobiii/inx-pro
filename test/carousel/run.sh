#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
layout_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/inx-carousel.XXXXXX")
trap 'rm -rf "$layout_test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc test/carousel/CarouselLayoutTest.cpp -o "$layout_test_dir/layout"
"$layout_test_dir/layout"
