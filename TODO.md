# TODO

1. ~~Try kernel polling~~
2. Handle ring queue full
3. If doesn't work
    - try with multiple (small) buffers
        - read using scatter/gather!
        - iov could be faster!

4. MUST handle cqes properly
    - what if a write doesn't complete?

5. cqes handled in the end
    - ~~must handle queue limit~~
        - ~~handle cqes~~
        - ~~handle max files open~~
            - add to report
    - ~~must handle close~~
        - add to report
    - make benchmark scripts for multi-level file
        - use generator.py

6. Dir. I: Allow multiple buffers
    - same memory footprint
    - test on a dir with a large no. of files

6. microbenchmark fcp using `perf` (other tools?)

7. Dir. II: Descriptorless I/O

8. RAM Disk

9. Report
    - justify decisions:
        - what is the "cost" of one syscall?
        - in comparison with copying from disk to file?
    

## Multifile

1. Benchmark on linux source code
    - with diff block size?

2. Handle cqe ringsize
    - how many should I clean up?



1. For fairness, our own version of standard `cp` 
2. block size, ring queue size, 

3. Benefits from two areas:
    - async in another kernel thread -- parallel processing of userspace and system call
        - user time is negligible (~1-2us)
    - syscall overhead
        - ???
        - strace
            - million read/write, ~100 async version -- still no benefit

4. Kernel Polling mode:

4. RAMDisk

Next steps:

## Report

- For large file sizes (1GB+), time to copy dominates in the program irrespective of the buffer size chosen
    - no discernable benefits (time to copy within 2%)
- To keep time to copy small wrt to program execution time: small file, even small buffer
    - multiple read/write
    - io_uring setup is considerable

- syscall time is negligible: 4M in standard cp, vs ~150 in cp using io_uring.

- profiled via strace and perf

- only benefit -- "free" concurrency; can do a bunch of tasks

- flush filesystem buffer cache before running the benchmarks
- a barebones version of standard cp, made by us
- ramdisk
- parameters tuned:
    - file size
    - buffer size
    - size of async I/O queue
    - polling kernel thread
- latest version of io_uring
    - compiled the kernel ourselves by taking the latest + applying (unreleased) patches
- profile: time taken by syscalls by strace, and time taken by our program by perf