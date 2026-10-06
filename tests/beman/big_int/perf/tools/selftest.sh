#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# SPDX-License-Identifier: BSL-1.0
# selftest.sh <shape_sweep binary>: run every op with every applicable row at small shapes (1 round, 2 ms).
# The harness aborts on any correctness mismatch, so exit status 0 means all rows agree. Timings are not meaningful.
set -u
B=${1:?usage: selftest.sh <shape_sweep binary>}
rc=0
while IFS= read -r spec; do
    [ -z "$spec" ] && continue
    op=${spec%% *}
    shapes=${spec#* }
    # shellcheck disable=SC2086
    if ! "$B" "$op" --rows auto,inplace,kernel,kernelip,floor,gmp,gmpz,builtin,copy --rounds 1 --round-ms 2 $shapes >/dev/null; then
        echo "selftest: FAILED $spec" >&2
        rc=1
    else
        echo "selftest: ok $spec"
    fi
done <<'SPECS'
add 1x1 4x4 16x16 64x64 1024x1 2000x2000
sub 1x1 4x4 16x16 64x64 1024x1 2000x1000
shl 1x13 4x13 16x13 256x13 256x77 2000x64
shr 1x13 4x13 16x13 256x13 256x77 2000x64
cmp 1 4 16 256
mul 1x1 3x3 16x16 64x64 100x10
sqr 1 4 16 64 512
divrem 2x1 8x4 32x16 128x64 2000x100 65536x1
div 8x4
tochars 1x10 4x10 16x10 64x10 256x10 16x16 2000x10
fromchars 1x10 4x10 16x10 64x10 256x10 2000x10
gcd 2x2 4x4 16x16 64x64 256x256 100x30
vecsort 1000x1
SPECS
exit $rc
