#include <stdio.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <liburing.h>
#include <string.h>

int main() {
    int ret;
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    ret = io_uring_queue_init(2, &ring, 0);
    assert(ret == 0);

    sqe = io_uring_get_sqe(&ring);
    assert(sqe != NULL);

    io_uring_prep_mkdirat(sqe, -1, "/home/ubuntu/project/aos_project/dst_dir", 0777);
    ret = io_uring_submit(&ring);
    assert(ret == 1);

    ret = io_uring_wait_cqe(&ring, &cqe);
    assert(ret == 0);

    printf("The result of mkdir operation is %d\n", cqe->res);
    if(cqe->res != 0) {
        fprintf(stderr, "Failed: %s\n", strerror(-cqe->res));
        exit(1);
    }

    return 0;
}

