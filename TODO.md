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

## Tasks
1. multiple buffers for single file
    - if speedup from parallel writes to memory, perf for a single file++
2. cost of atomics
    - also for 1M 1B files
    - benchmark `__io_uring_peek_cqe`
3. Turn off fadvise/readahead
    - does multifile get better?
4. See cache hit/miss
    - is the benefit really due to readahead?
    - https://lwn.net/Articles/155510/

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
    3. multiple really small 1B files (\#bufs = 1)
        - syscall should dominate --> it doesn't :(
        - ~~maybe cost of atomic?~~ Unlikely, 100s of cycles at max
        - wait_nr does a system call

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

7. ~~Number of files open together~~
    - openat is insanely slow sometimes -- **slowest** system call
        - should provide ~5% improvement! ! !

- Maybe use the Linux repo when not varying file/dir params
- include compile options
- include standard deviations
- include how we `sync` and drop the buffer cache before running


## Report

1. io_uring overview

3. potential benefits
    - async: 
        - free parallism b/w I/O and compute
        - multi-threaded I/O, to saturate the device bandwidth
        - threads managed automatically
    - (almost) no syscall overhead: SQPOLL has 0, in theory

4. problems
    - some syscalls not supported: fstatat, mmap
        - replacement requires two separate calls :(
    - interrupt-driven wait, or poll on an atomic
        - both have significant overhead w.r.t. system call
        - but once every batch
        - (TODO: add benchmark results)
    - max queue size
        - forced to be sequential after queue is full
    - no fine grained control on threads
    - imp issues
      - LINKs are linear, also not across multiple submit calls
    - polling API --> my god.

5. Implementation Details

6. Experiments & Results

### Results

0. Preliminaries:
    - barebones cp, for fairness
        - reasonably fast, matches `cp` for most purposes
        - modern c++: string_view, etc.
        - doesn't support all functions
            - does support ....
    - latest version of io_uring
        - compiled the kernel ourselves by taking the latest + applying (unreleased) patches
    - tmpfs
    - sync + cache drop
        - caching covered in 
    - parameters
    - scheduler, cpu-freq
    - interrupt-based vs polling
        - doesn't change results
    - ???

1. Performance on a single large file is not better:
    - async b/w I/O and compute does not matter
        - 'user' time when running `cp` is negligible (0.004 / 3.870 = 0.1% when copying 2GB on an SSD, less than noise)
    - multi-threaded I/O doesn't make sense
        - reads/writes are done in series, for readahead
            - random I/O will be way slower
    - syscall overhead is also negligible
        - (add `strace -c` values)
        - (note `openat`, and others values, usually <100us!)
    - note that still have the benefits of being async
        - user thread can do some other work

2. Performance on a single large file on tmpfs is not better:
   

3. Performance for a single small file is not better:
    - async b/w I/O and compute doesn't matter
        - 'user' time when running `cp` is 0
    - syscall overhead is also negligible 
        - (add `strace -c` values): `strace` shows system call takes <=100us (\~1% of total running time)
        - actual overhead will be even less


4. Multi-file/large buffer is much better
    - cp w/ RANDOM is SLOW
        - larger buffer helps a tiny bit here (3.8 to 3.5 when 1GB multi file)
        - because reading sequentially from disk in one request is slightly faster, even for SSD
    - fcp remains the same! ! !
    - **Reason**: queue depth. queuing up multiple read requests is much faster! !
        - test: iostat
        - test: reduce queue_depth to 1
            - `echo 1 | sudo tee /sys/bus/scsi/devices/<SCSI-DEVICE>/queue_depth`
            - Ref: https://www.ibm.com/docs/en/linux-on-systems?topic=devices-setting-queue-depth
            - Note that SATA devices speak SCSI to the kernel's generic disk driver, hence in scsi!
    **Discarded results**
        - saturating bandwidth -- cp w/ larger buffer should do better
        - readahead theory
            - disabling: only worsens `cp`'s performance, `fcp` remains the same
        - reads being merged! ! !
            - iostat says otherwise!
            - most likely that our buffer was already big enough that merging doesn't make much sense
        - NUMA nodes
          - each socket have its own bus
          - Total memory bandwidth = 2*PerNodeBandwidth
            - nope: `numactl -N 0 -m 0` doesn't change things :(

## PPT
- must mention system specs:
    - include SATA SSD!!!
 - include how atomics have a cost too
 - why did we do reads/writes serially
    - well, parallel would require higher buffer size
        - unfair to regular cp
    - we wanted to see benefit of async + system call
 - show strace system call cost (lower bound!)
    - include that 4M vs 100 visual!!!
    - actual overhead is muchhhhh lower
    - everyone keeps crying about system call overhead, context switching costs
        - my cp will save the linux community years!
- mention that its still async
    - free concurrency!

- kept trying to optimize/profile our code
    - 
- SSD Driver: https://www.yellow-bricks.com/2014/06/09/queue-depth-matters/


### Profiling


### Implementation Details

1. Handle ring queue size
    - possible to deal with NO_CQE_ON_SUCCESS
2. Handling \# of files that you can open at once
3. Handling limited \# of buffers
    - future work: use a better memory allocator

4. We also tried a fully pipelined version
    - DIAGRAM! ! !
    - Explain how it worked!
    - it performed slower --> we shifted to a simpler implementation
    - Reasons (+ data!) to believe that a pipelined version, however well optimized, won't do any better.
    - similar for multithreaded


### Other Sections


- For large file sizes (1GB+), time to copy dominates in the program irrespective of the buffer size chosen
    - no discernable benefits (time to copy within 2%)
    - Amdahl's Law
- To keep time to copy small wrt to program execution time: small file, even small buffer
    - multiple read/write
    - io_uring setup is considerable
- Adaptive File Read-Ahead!
    - single file :(

- syscall time is negligible: 4M in standard cp, vs ~150 in cp using io_uring.

- profiled via strace and perf

- only benefit -- "free" concurrency; can do a bunch of tasks

- flush filesystem buffer cache before running the benchmarks

- ramdisk
- parameters tuned:
    - file size
    - buffer size
    - size of async I/O queue
    - polling kernel thread

- profile: time taken by syscalls by strace, and time taken by our program by perf