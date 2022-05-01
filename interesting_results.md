## Interesting Results
1. Multi File filesize -- mf_file_size_results.json
    - Submitting multiple write operations in parallel helps utlize more bandwidth -- maybe they aren't queued in the FS (tmpfs)/VFS layer?
    - This happens when the filesize is much greater than each 'division' of the file buffer
2. 
    



### Single-File

Compare time taken by cp and fcp over:

~~1. Diff buffer sizes~~
    - w/ SQPOLL
    - on tmpfs
~~2. Diff ring queue sizes~~
    - w/ SQPOLL
    - on tmpfs
~~3. Diff file sizes~~

Compare \# of system-calls over

1. diff file sizes
    - w/ SQPOLL

### Multifile

Compare time taken by cp and fcp over:

~~1. \# of files: 10 - 10000~~
    ~~- w/ SQPOLL~~
    ~~- on tmpfs~~
~~2. Size of each file: 1KB - 1GB~~
    ~~- w/ SQPOLL~~
    ~~- on tmpfs~~
3. Size of buffer
    - w/ SQPOLL
    - on tmpfs
~~4. \# of buffers~~
    ~~- w/ SQPOLL~~
    ~~- on tmpfs~~
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