#include <iostream>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <dirent.h>
#include <liburing.h>

using std::endl;
using std::cout;
using std::cerr;

#define RINGSIZE 32
#define IORING_OP_GETDENTS64 41
#define NUMSLOTS 10


static inline void io_uring_prep_getdents64(struct io_uring_sqe *sqe, int fd,
					    void *buf, unsigned int count)
{
	io_uring_prep_rw(IORING_OP_GETDENTS64, sqe, fd, buf, count, 0);
}

struct linux_dirent64 {
	int64_t		d_ino;    /* 64-bit inode number */
	int64_t		d_off;    /* 64-bit offset to next structure */
	unsigned short	d_reclen; /* Size of this dirent */
	unsigned char	d_type;   /* File type */
	char		d_name[]; /* Filename (null-terminated) */
};


int main() {
    int ret;
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct linux_dirent64 *dirent;
    uint8_t	dirbuf[16384];
    int files[NUMSLOTS];
    int num_to_wait;
    int fd;
    int i;
    int slot = 0;
    uint8_t *bufp;

    for(i=0; i<NUMSLOTS; i++) {
        files[i] = -1;
    }
    
    // Initialize the queue
    ret = io_uring_queue_init(RINGSIZE, &ring, 0);
    if(ret != 0) {
        perror("ring init");
        exit(1);
    }

    // Register fds
    io_uring_register_files(&ring, files, NUMSLOTS);

    // Get sqes for opening dir
    sqe = io_uring_get_sqe(&ring);
    if(sqe == NULL) {
        cerr << "failed to get a free sqe" << endl;
        exit(1);
    }

    // Prepare open dir request
    io_uring_prep_openat_direct(sqe, AT_FDCWD, "test_dir", O_DIRECTORY, 0, slot);
    sqe->flags = IOSQE_IO_LINK;

    // Get new sqe for readdir
    sqe = io_uring_get_sqe(&ring);
    if(sqe == NULL) {
        cerr << "failed to get a free sqe" << endl;
        exit(1);
    }

    // int test = open("test_dir", O_DIRECTORY);
    // assert(test >= 0);

    // Prepare the readdir request
    io_uring_prep_getdents64(sqe, slot, dirbuf, sizeof(dirbuf));
    sqe->flags = IOSQE_FIXED_FILE;

    num_to_wait = 2;
    ret = io_uring_submit_and_wait(&ring, num_to_wait);
    if(ret < num_to_wait) {
        cerr << "submit and wait failed with " << ret << " submissions successful" << endl;
        exit(1);
    }

    ret = io_uring_wait_cqe(&ring, &cqe);
    if(ret != 0) {
        perror("wait cqe");
        exit(1);
    }
    printf("The address of first cqe = %p\n", cqe);

    printf("The result of opendir = %d\n", cqe->res);
    if(cqe->res < 0) {
        printf("Error msg: %s\n", strerror(-cqe->res));
        exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);

    ret = io_uring_wait_cqe(&ring, &cqe);
    if(ret != 0) {
        perror("wait cqe");
        exit(1);
    }
    printf("The address of second cqe = %p\n", cqe);

    printf("The result of getdents = %d\n", cqe->res);
    if(cqe->res < 0) {
        printf("Error msg: %s\n", strerror(-cqe->res));
        exit(1);
    }

    bufp = dirbuf;

    dirent = (struct linux_dirent64*)bufp;
    printf("The direntry name = %s\n", dirent->d_name);
    printf("Is directory = %d\n", dirent->d_type == DT_DIR);

    bufp += dirent->d_reclen;

    dirent = (struct linux_dirent64*)bufp;
    printf("The direntry name = %s\n", dirent->d_name);
    printf("Is directory = %d\n", dirent->d_type == DT_DIR);

    bufp += dirent->d_reclen;

    dirent = (struct linux_dirent64*)bufp;
    printf("The direntry name = %s\n", dirent->d_name);
    printf("Is directory = %d\n", dirent->d_type == DT_DIR);

    bufp += dirent->d_reclen;

    dirent = (struct linux_dirent64*)bufp;
    printf("The direntry name = %s\n", dirent->d_name);
    printf("Is directory = %d\n", dirent->d_type == DT_DIR);

    return 0;
}
