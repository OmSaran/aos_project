#include <iostream>
#include <vector>
#include <array>
#include <filesystem>

#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <liburing.h>

#define RING_SIZE 256
#define DIR_BUF_SIZE 65536

struct io_uring ring;

using namespace std;

#define IORING_OP_GETDENTS64 41
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

class IORing {
private:
    struct io_uring ring;
public:
    IORing(ssize_t size) {
        int ret = io_uring_queue_init(RING_SIZE, &ring, 0);
        assert(ret == 0);
    }

    io_uring_sqe* get_sqe() {
        struct io_uring_sqe *sqe;
        sqe = io_uring_get_sqe(&ring);
        assert(sqe != NULL);
        return sqe;
    }

    io_uring_cqe* submit_and_wait() {
        struct io_uring_cqe *cqe;
        io_uring_submit_and_wait(&ring, 1);
        io_uring_wait_cqe(&ring, &cqe);

        return cqe;
    }
};

class CopyJobInfo {
public:
    filesystem::path src;
    filesystem::path dst;
    int src_fd;
    int dst_fd;

    CopyJobInfo(filesystem::path src_path, filesystem::path dst_path) 
        : src{src_path}, dst{dst_path}
    {}
};

class DirJob {
private:
    vector<uint8_t> dirent_buf;
    filesystem::path dir_path;
    bool is_opened;
    bool dir_fetched;
    int dir_fd;
    int size;
    IORing& ring;

    void open_dir() {
        if(is_opened)
            assert(0);
        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;
        sqe = ring.get_sqe();
#ifdef FD_DIRECT
#else
        io_uring_prep_openat(sqe, -1, dir_path.c_str(), O_DIRECTORY, 0);
        cqe = ring.submit_and_wait();
        assert(cqe->res >= 0);
        dir_fd = cqe->res;
#endif
    }

public:
    DirJob(filesystem::path d_path, IORing& ioring)
        : dir_path{d_path}, ring{ioring}, is_opened{false}, dir_fetched{false}
    {
        this->dirent_buf.resize(DIR_BUF_SIZE);
    }

    //! FIXME: Loop over this
    void fetch_dir() {
        if(!is_opened)
            open_dir();

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        sqe = ring.get_sqe();
        io_uring_prep_getdents64(sqe, dir_fd, (void *)&dirent_buf[0], dirent_buf.size());
        cqe = ring.submit_and_wait();

        assert(cqe->res >= 0);
        dirent_buf.resize(cqe->res);
    }

    void do_stat(vector<CopyJobInfo>& copy_info) {
        // batch all stats
    }

    void do_copy_files(vector<CopyJobInfo>& copy_info) {
    }

    vector<filesystem::path> copy_dir_files(filesystem::path dst_path) {
        if(!dir_fetched)
            fetch_dir();

        uint8_t *bufp;
        uint8_t *end;

        filesystem::path src_path, dst_path, dst_dir;

        bufp = (uint8_t *)&dirent_buf[0];
        end = bufp + dirent_buf.size();

        vector<filesystem::path> directories;
        vector<CopyJobInfo> copy_files;
        vector<CopyJobInfo> copy_dirs;

        while (bufp < end) {
            struct linux_dirent64 *dent;

            dent = (struct linux_dirent64 *)bufp;
            if (strcmp(dent->d_name, ".") && strcmp(dent->d_name, "..")) {
                // Create copy jobs;
                src_path = dir_path / dent->d_name;
                dst_path = dst_path / dent->d_name;
                if(dent->d_type == DT_REG) {
                    // copy the files
                    CopyJobInfo info(src_path, dst_path);
                    copy_files.push_back(info);
                } 
                else if (dent->d_type == DT_DIR) {
                    // create the directories
                    CopyJobInfo info(src_path, dst_path);
                    copy_dirs.push_back(info);
                    directories.push_back(src_path);
                }
            }
            bufp += dent->d_reclen;
        }

        do_copy_files(copy_files);

        return directories;
    }
};

int main() {
    IORing ring(RING_SIZE);
}
