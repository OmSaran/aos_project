#!/bin/bash


echo "Sanity Checks"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <cp-executable> \"[opts]\"" >&2
    exit 1
fi

if ! [ -x "$1" ]; then
    echo "$1 is not executable" >&2
    exit 1
fi

exec="$1"
if [ $# -eq 2 ]; then
    exec="$1 $2"
fi

echo "Test #1: Copy single file"
dd if=/dev/urandom of=_test bs=1M count=10 status=none
./$exec _test _test.copy
cmp -s _test _test.copy
if [ $? -ne 0 ]; then
    echo "test failed, files are not the same."
    # rm _test _test.copy
    exit 0
fi
echo "Passed!"
rm _test.copy

echo "Test #2: Copy file to directory"
mkdir _testdir
./$exec _test _testdir/
cmp -s _test _testdir/_test
if [ $? -ne 0 ]; then
    echo "test failed, files are not the same."
    rm -r _testdir _test
    exit 0
fi
echo "Passed!"
rm _test

echo "Test #3: Copy directory to directory"
mkdir _testdir2
./$exec -r _testdir _testdir2/
cmp -s _testdir/_test _testdir2/_testdir/_test
if [ $? -ne 0 ]; then
    echo "test failed, files are not the same."
    rm -r _testdir _testdir2
    exit 0
fi
echo "Passed!"
rm -r _testdir _testdir2
