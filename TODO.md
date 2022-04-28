# TODO

1. ~~Try kernel polling~~
2. ~~Handle ring queue full~~
3. ~~If doesn't work~~
    - try with multiple (small) buffers
        - read using scatter/gather!
        - iov could be faster!

4. MUST handle cqes properly
    - what if a write doesn't complete?

5. ~~cqes handled in the end~~
    - ~~must handle queue limit~~
        - ~~handle cqes~~
        - ~~handle max files open~~
            - add to report
    - ~~must handle close~~
        - add to report
    - ~~make benchmark scripts for multi-level file~~
        - use generator.py

6. ~~Dir. I: Allow multiple buffers~~
    - same memory footprint
    - test on a dir with a large no. of files

7. ~~microbenchmark fcp using `perf` (other tools?)~~

8. More Optimizations
    - free bufs faster
    - descriptorless I/O
        - must do everything with io_uring
    - do open()/fstat with io_uring as well?
        - eh


9. ~~RAM Disk~~

10. Graphs

### Tasks
1. multiple buffers for single file
    - if speedup from parallel writes to memory, perf for a single file++
2. cost of atomics    

11. Explain results:
    1. single file in SSD/Memory: 0 benefits
        - reads/writes are serial
        - syscall overhead is ~0
        - program length wrt copying time is ~0
        - for super large files, ring length becomes the bottleneck
            - not everything can be async
        - for small files, io_uring atomics + init is a bottleneck
            - io_uring_setup take **200us+**
                - 128KB `read` from fs cache is only **17us**
                - 128KB `read` from disk is **500us**
            - TODO: multiple buffers for single file
    2. multiple file in SSD
        - io_uring: parallel read/write
            - most reads/writes are from/to memory, memory bandwidth is insane!
            - (Why not use a large blocksize?)
                - `cp` can only use blocksize <= filesize, since serial
        - io_uring: initiate readahead on multiple files
            - happens in parallel with **copying**
    3. io_uring ops aren't costless
        - atomic
    4. Pipeline stalls
        - `openat`: locks the entire dir tree; if needs disk access then dead
            - opening multiple files: pressure on disk
    - diagram: first read, memcpy, readahead in parallel
    - syscalls overhead is negligible
        - do read & write on `/dev/zero`
        - do read & write on tmpfs, and subtrace time to write to a block
            - strace? Or simple clock_gettime
    - for large single files, IO time >> rest of the program
        - strace -T
            - compare time taken by read/write calls vs program execution time
    - io_uring_setup takes time
        - strace -T
    - readahead works really well
        - some of it is already concurrent
        - use `perf`, see cache hit
        - run without madvise
    - some tasks are parallizable
        - modifying cached fs metadata
    - ~~are reads being merged???~~
        - `iostat`: https://linux.die.net/man/1/iostat

12. Results
    - benefits because of multi-threaded I/O
        - not multi-threaded execution
        - not syscall overhead

## Graphs

### Single-File

Compare time taken by cp and fcp over:

1. Diff buffer sizes
    - w/ SQPOLL
    - on tmpfs
2. Diff ring queue sizes
    - w/ SQPOLL
    - on tmpfs
3. Diff file sizes

Compare \# of system-calls over

1. diff file sizes
    - w/ SQPOLL

### Multifile

Compare time taken by cp and fcp over:

1. \# of files: 10 - 10000
    - w/ SQPOLL
    - on tmpfs
2. Size of each file: 1KB - 1GB
    - w/ SQPOLL
    - on tmpfs
3. Size of buffer
    - w/ SQPOLL
    - on tmpfs
4. \# of buffers
    - w/ SQPOLL
    - on tmpfs
        - can maybe merge with size of buffer!
5. Depth of FS:
    - since open/close aren't async
    - on tmpfs
6. Diff ring queue sizes
    - w/ SQPOLL
    - on tmpfs

7. Number of files open together
    - openat is insanely slow sometimes -- **slowest** system call
        - should provide ~5% improvement! ! !

- Maybe use the Linux repo when not varying file/dir params
- include compile options
- include standard deviations
- include how we `sync` and drop the buffer cache before running

### Optional (??)


## Misc.
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

### Profiling

1. calc hit-rate/miss-rate
    - for single file, should be incredible high
    - turn off fadvise --> should go down????
2. strace -T
3. Time taken by I/O vs time taken by processing


### Implementation Details

1. Handle ring queue size
    - possible to deal with NO_CQE_ON_SUCCESS
2. Handling \# of files that you can open at once
3. Handling limited \# of buffers
    - future work: use a better memory allocator

- What we tried/failed/didn't do
    - fully pipelined version: bad performance (why?)
    - threads: justify


### Other Sections

- justify decisions:
    - what is the "cost" of one syscall?
    - in comparison with copying from disk to file?
    - SSD Driver: https://www.yellow-bricks.com/2014/06/09/queue-depth-matters/

- For large file sizes (1GB+), time to copy dominates in the program irrespective of the buffer size chosen
    - no discernable benefits (time to copy within 2%)
    - Amdahl's Law
- To keep time to copy small wrt to program execution time: small file, even small buffer
    - multiple read/write
    - io_uring setup is considerable
- Adaptive File Read-Ahead!
    - https://lwn.net/Articles/155510/
    - single file :(

- syscall time is negligible: 4M in standard cp, vs ~150 in cp using io_uring.

- profiled via strace and perf

- only benefit -- "free" concurrency; can do a bunch of tasks

- flush filesystem buffer cache before running the benchmarks
- a barebones version of standard cp, made by us
    - reasonably fast, matches `cp` for most purposes
    - modern c++: string_view, etc.
    - doesn't support all functions
        - does support ....
- ramdisk
- parameters tuned:
    - file size
    - buffer size
    - size of async I/O queue
    - polling kernel thread
- latest version of io_uring
    - compiled the kernel ourselves by taking the latest + applying (unreleased) patches
- profile: time taken by syscalls by strace, and time taken by our program by perf