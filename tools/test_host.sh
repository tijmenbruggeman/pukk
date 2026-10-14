#!/bin/sh
# Host tests that need no hardware: builds the sample pack with tools/pukk.py
# and runs the pack format reader against it, then the battle rules and
# messages; all with address/UB sanitizers.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

python3 "$root/tools/pukk.py" build "$root/packs/voorbeeld.json" -o "$out/voorbeeld.pukk" 2>/dev/null

cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$root/components/packs/include" \
    "$root/components/packs/pack_format.c" \
    "$root/components/packs/test/test_pack_format.c" \
    -o "$out/test_pack_format"

"$out/test_pack_format" "$out/voorbeeld.pukk"

cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I "$root/components/battle/include" \
    "$root/components/battle/battle_rules.c" \
    "$root/components/battle/battle_msg.c" \
    "$root/components/battle/test/test_battle.c" \
    -o "$out/test_battle"

"$out/test_battle"
