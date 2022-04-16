#!/bin/bash


echo "Sanity Checks"

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <cp-executable>" >&2
    exit 1
fi

if ! [ -x "$1" ]; then
    echo "$1 is not executable" >&2
    exit 1
fi

echo "Test #1: Copy single file"
dd if=/dev/urandom of=_test bs=1M count=10 status=none
./"$1" _test _test.copy
cmp -s _test _test.copy
if [ $? -ne 0 ]; then
    echo "test failed, files are not the same."
    rm _test _test.copy
    exit 0
fi
echo "Passed!"
rm _test _test.copy
