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
cmake ..
make
```

## Report

1. Handle ringbuffer size
2. handle max open files