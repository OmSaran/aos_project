# Advanced Operating Systems Project

## Requirements

1. Library Dependencies
    ```
    liburing
    ```

2. Programs should have `CAP_SYS_NICE` capability
    ```
    sudo setcap 'cap_sys_nice=eip' fcp
    sudo setcap 'cap_sys_nice=eip' fcp2
    ```


## Build
```
mkdir build; cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Run
The interface is similar to GNU cp. For eg., to make a copy of `dir1` called `copy_dir1`

```bash
./fcp -r dir1/ /path/to/copy_dir1/
```

Features / Flags:

| Flag | Description | cp | fcp |
|------|-------------|----|-----|
| -h   | Prints help | &check; | &check; |
| -r   | copy recursively | &check; | &check; |
| -b B | total buffer size in KiB (default: 128KiB) | &check; | &check; |
| -n N | # of sub-buffers to use (default: 2) |  | &check; |
| -k   | (io_uring) use a separate kernel thread to poll SQ (default: false) |  | &check; |
| -q Q | size of SQ (default: 16384) | | &check; |

## Benchmarks & Tests

All benchmark scripts and tests can be found inside the [tests/](./tests/) folder.

1. [sanity_checks.sh](./tests/sanity_checks.sh): Runs some basic sanity checks by copying files/folders and verifying correctness via `diff`:
    ```bash
    ./sanity_checks  ../build/fcp
    ```

2. [benchmark_single.sh](./tests/benchmark_single.sh): Benchmarks the time taken to copy a single file
    ```bash
    sudo ./benchmark.sh <size-in-MB> <cp-executable> [opts] 
    ```

3. [benchmark_multi.sh](./tests/benchmark_multi.sh): Benchmarks the time taken to copy a folder with multiple files
    ```bash
    sudo ./benchmark.sh <size-in-MB> <num-files> <cp-executable> [opts] 
    ```

4. (runall.sh?)

## Results

(How to generate?)
(How to make graphs?)