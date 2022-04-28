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