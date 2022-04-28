//! WARNING: Not thread safe
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
    int size;
    int running_tasks;
public:
    IORing(ssize_t size) {
        this->size = size;
        this->running_tasks = 0;
        int ret = io_uring_queue_init(size, &ring, 0);
        assert(ret == 0);
    }

    int num_tasks_running() {
        return running_tasks;
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

    void submit_and_wait_nr(int nr) {
        int ret = io_uring_submit_and_wait(&ring, nr);
        assert(ret == nr);
        running_tasks += nr;
    }

    io_uring_cqe* get_cqe() {
        struct io_uring_cqe *cqe;
        int ret;

        ret = io_uring_wait_cqe(&ring, &cqe);
        assert(ret == 0);

        running_tasks -= 1;
        return cqe;
    }
};

class CopyJobInfo {
public:
    filesystem::path src;
    filesystem::path dst;
    int src_fd;
    int dst_fd;
    struct statx st_buf;

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
        int num = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_tasks_running() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            io_uring_prep_statx(sqe, -1, info.src.c_str(), 0, STATX_SIZE, &info.st_buf);
            sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
        }
        ring.submit_and_wait_nr(copy_info.size());
    }

    void do_src_opens(vector<CopyJobInfo>& copy_info) {
        // src file opens
        int num = 0;
        int idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_tasks_running() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            io_uring_prep_openat(sqe, -1, info.src.c_str(), O_RDONLY, 0);
            io_uring_sqe_set_data(sqe, (void *)idx);

            idx += 1;
        }
        ring.submit_and_wait_nr(copy_info.size());

        for(int i=0; i<copy_info.size(); i++) {
            cqe = ring.get_cqe();
            assert(cqe->res >= 0);
            copy_info[(int)cqe->user_data].src_fd = cqe->res;
        }
    }

    void do_dst_opens(vector<CopyJobInfo>& copy_info) {
        // src file opens
        int num = 0;
        int idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_tasks_running() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            io_uring_prep_openat(sqe, -1, info.dst.c_str(), O_WRONLY, 0);
            io_uring_sqe_set_data(sqe, (void *)idx);

            idx += 1;
        }
        ring.submit_and_wait_nr(copy_info.size());

        for(int i=0; i<copy_info.size(); i++) {
            cqe = ring.get_cqe();
            assert(cqe->res >= 0);
            copy_info[(int)cqe->user_data].dst_fd = cqe->res;
        }
    }

    void do_data_copy_serial_linked(vector<CopyJobInfo>& copy_info) {
        
    }

    //! WARNING: Assumes there are no other tasks running in ring
    void do_copy_files(vector<CopyJobInfo>& copy_info) {
        do_stat(copy_info);
        do_src_opens(copy_info);
        do_dst_opens(copy_info);
        do_data_copy_serial_linked(copy_info);
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
