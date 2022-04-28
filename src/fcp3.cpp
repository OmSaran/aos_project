//! WARNING: Not thread safe
#include <iostream>
#include <vector>
#include <queue>
#include <array>
#include <filesystem>

#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <liburing.h>

#define RING_SIZE 1024
#define DIR_BUF_SIZE 65536
#define RW_BUF_SIZE 128 * 1024
#define BILLION 1000000000L


struct timespec total_time;
struct timespec setup_time;
struct timespec src_open_time;
struct timespec dst_open_time;
struct timespec stat_time;
struct timespec data_copy_time;

struct io_uring ring;

using namespace std;

#define IORING_OP_GETDENTS64 41
static inline void io_uring_prep_getdents64(struct io_uring_sqe *sqe, int fd,
					    void *buf, unsigned int count, long offset)
{
	io_uring_prep_rw(IORING_OP_GETDENTS64, sqe, fd, buf, count, offset);
}

void get_time(struct timespec *tp);
struct timespec get_diff(struct timespec *start, struct timespec *end);
void update_ts(struct timespec *target, struct timespec *diff);


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
    int cqe_pending_tasks;
    int max_open_files;
public:
    IORing(ssize_t size) {
        this->size = size;
        this->cqe_pending_tasks = 0;
        this->max_open_files = 1024;
        int ret = io_uring_queue_init(size, &ring, 0);
        assert(ret == 0);
    }

    int ring_size() {
        return size;
    }

    int num_cqe_pending_tasks() {
        return cqe_pending_tasks;
    }

    io_uring_sqe* get_sqe() {
        struct io_uring_sqe *sqe;
        sqe = io_uring_get_sqe(&ring);
        assert(sqe != NULL);
        return sqe;
    }

    // int submit(bool skip) {
    //     io_uring_submit(&ring);
    //     if(!skip)
    //         cqe_pending_tasks += 1;
    // }

    int space_left() {
        return io_uring_sq_space_left(&ring);
    }

    void submit_and_wait(io_uring_cqe **cqe) {
        int ret;

        io_uring_submit_and_wait(&ring, 1);
        ret = io_uring_wait_cqe(&ring, cqe);

        cqe_pending_tasks += 1;

        assert(ret == 0);
    }

    void see_cqe(io_uring_cqe *cqe) {
        io_uring_cqe_seen(&ring, cqe);

        cqe_pending_tasks -= 1;
    }

    void submit_and_wait_nr(int nr, int num_skipped) {
        int ret = io_uring_submit_and_wait(&ring, nr-num_skipped);
        assert(ret == nr);
        cqe_pending_tasks += (nr-num_skipped);
    }

    void get_cqe(io_uring_cqe **cqe) {
        int ret;

        ret = io_uring_wait_cqe(&ring, cqe);
        assert(ret == 0);
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
    filesystem::path dst_dir_path;
    bool is_opened;
    bool dir_fetched;
    bool dst_created;
    int dir_fd;
    int dst_dir_fd;
    int size;
    int buf_size;
    size_t dir_buf_size;
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
        ring.submit_and_wait(&cqe);
        if(cqe->res < 0) {
            cerr << "Failed to open directory: " << dir_path.c_str() << " : " << strerror(-cqe->res) << endl;
            exit(1);
        }
        dir_fd = cqe->res;
        ring.see_cqe(cqe);
#endif
    }

public:
    DirJob(filesystem::path src_path, filesystem::path dst_path, IORing& ioring, int bufsize)
        : dir_path{src_path}, dst_dir_path{dst_path}, ring{ioring}, buf_size{bufsize}
    {
        this->dir_buf_size = DIR_BUF_SIZE; 
        this->dirent_buf.resize(this->dir_buf_size);
        is_opened = false;
        dir_fetched = false;
    }

    void fetch_dir() {
        if(!is_opened)
            open_dir();

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;
        int res;
        int offset = 0;

        sqe = ring.get_sqe();

        assert(ring.num_cqe_pending_tasks() == 0);

        io_uring_prep_getdents64(sqe, dir_fd, (void *)&dirent_buf[0], this->dir_buf_size, offset);
        ring.submit_and_wait(&cqe);
        res = cqe->res;
        cout << "got " << res << " bytes in getdents" << endl;
        assert(res >= 0);
        ring.see_cqe(cqe);
        dirent_buf.resize(res);
        
        while(res != 0) {
            dirent_buf.resize(dirent_buf.size() + this->dir_buf_size);

            sqe = ring.get_sqe();

            offset = dirent_buf.size()-this->dir_buf_size;

            io_uring_prep_getdents64(sqe, dir_fd, (void *)&dirent_buf[offset], this->dir_buf_size, offset);
            ring.submit_and_wait(&cqe);
            res = cqe->res;
            cout << "got " << res << " more bytes in getdents for offset " << offset << endl;
            assert(res >= 0);
            ring.see_cqe(cqe);
            dirent_buf.resize(dirent_buf.size() - (this->dir_buf_size - res));
        }
    }

    void do_stat(vector<CopyJobInfo>& copy_info) {
        // batch all stats
        int num = 0;
        int ret;
        int count = 0;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            io_uring_prep_statx(sqe, -1, info.src.c_str(), 0, STATX_SIZE, &info.st_buf);
            // sqe->flags |= IOSQE_ASYNC;
            if(count != copy_info.size()-1) {
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
            }
            count += 1;
        }
        ring.submit_and_wait(&cqe);
        ring.see_cqe(cqe);
    }

    void do_src_opens(vector<CopyJobInfo>& copy_info) {
        // src file opens
        int num = 0;
        long idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            // cout<< "Opening source directory: " << info.src << endl;
            io_uring_prep_openat(sqe, -1, info.src.c_str(), O_RDONLY, 0);
            // sqe->flags |= IOSQE_ASYNC;
            io_uring_sqe_set_data(sqe, (void *)idx);

            idx += 1;
        }
        ring.submit_and_wait_nr(copy_info.size(), 0);

        for(int i=0; i<copy_info.size(); i++) {
            ring.get_cqe(&cqe);
            long index = (long)cqe->user_data;
            // cout<< "index = " << index << endl;
            assert(cqe->res >= 0);
            // cout<< "Opened source directory: " << copy_info[index].dst << "; with result = " << cqe->res << endl;
            copy_info[index].src_fd = cqe->res;
            ring.see_cqe(cqe);
        }
    }

    void do_dst_opens(vector<CopyJobInfo>& copy_info) {
        // src file opens
        int num = 0;
        long idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(auto& info: copy_info) {
            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            //! TODO: Add fadvise
            // cout<< "Opening destination directory: " << info.dst << endl;
            io_uring_prep_openat(sqe, -1, info.dst.c_str(), O_CREAT | O_WRONLY, 0777);
            // sqe->flags |= IOSQE_ASYNC;
            io_uring_sqe_set_data(sqe, (void *)idx);

            idx += 1;
        }
        ring.submit_and_wait_nr(copy_info.size(), 0);

        for(int i=0; i<copy_info.size(); i++) {
            ring.get_cqe(&cqe);
            long index = (long)cqe->user_data;
            // cout<< "index = " << index << endl;
            assert(cqe->res >= 0);
            // cout<< "Opened destination directory: " << copy_info[index].dst << "; with result = " << cqe->res << endl;
            copy_info[index].dst_fd = cqe->res;
            ring.see_cqe(cqe);
        }
    }

    void do_data_copy_serial_linked(vector<CopyJobInfo>& copy_info) {
        void *buf = malloc(buf_size);

        int num = 0;
        int idx = 0;
        int ret;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        for(int i=0; i<copy_info.size(); i++) {
            size_t size = copy_info[i].st_buf.stx_size;
            size_t remaining = size;

            int src_fd = copy_info[i].src_fd;
            int dst_fd = copy_info[i].dst_fd;

            // cout<< "Copying the file: " << copy_info[i].src << "; size = " << copy_info[i].st_buf.stx_size << "; src_fd = " << src_fd << "; dst_fd = " << dst_fd << endl;

            while(remaining > 0) {
                int rw_size = remaining < buf_size ? remaining : buf_size;

                sqe = ring.get_sqe();
                io_uring_prep_read(sqe, src_fd, buf, rw_size, size-remaining);
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
                sqe->flags |= IOSQE_IO_LINK;
                // sqe->flags |= IOSQE_ASYNC;
                
                sqe = ring.get_sqe();
                io_uring_prep_write(sqe, dst_fd, buf, rw_size, size-remaining);
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
                sqe->flags |= IOSQE_IO_LINK;
                // sqe->flags |= IOSQE_ASYNC;

                num += 2;
                // If it is the last operation don't skip success -- we need to know when the last operation is completed.
                if(i == (copy_info.size()-1) && (remaining - rw_size) == 0) {
                    // cout<< "Last op in copying files" << endl;
                    sqe->flags &= ~(IOSQE_CQE_SKIP_SUCCESS);
                    sqe->flags &= ~(IOSQE_IO_LINK);
                }
                else if (ring.ring_size() - num <= 2) {
                    // // cout<< "Running out of space! " << endl;
                    sqe->flags &= ~(IOSQE_CQE_SKIP_SUCCESS);
                    sqe->flags &= ~(IOSQE_IO_LINK);

                    int num_submitted = num;
                    int num_skipped = num-1;

                    ring.submit_and_wait_nr(num_submitted, num_skipped);
                    ring.get_cqe(&cqe);
                    if(cqe->res < 0) {
                        cerr << "Failed in rw: " << strerror(cqe->res) << endl;
                        exit(1);
                    }

                    for(int j=0; j<num_submitted-num_skipped; j++) {
                        ring.see_cqe(cqe);
                    }

                    // // cout<< "ring.space_left() after clearing " <<  ring.space_left() << endl;
                    num = 0;
                } else {
                    // // cout<< "ring.space_left() " <<  ring.space_left() << endl;
                }

                remaining -= rw_size;
            }
        }        

        ring.submit_and_wait(&cqe);
        if(cqe->res < 0) {
            cerr << "Failed in rw after last op: " << strerror(cqe->res) << endl;
            exit(1);
        }
        assert(cqe->res >= 0);
        ring.see_cqe(cqe);
    }

    //! WARNING: Assumes there are no other tasks running in ring
    void do_copy_files(vector<CopyJobInfo>& copy_info) {
        if(copy_info.size() == 0) return;
        struct timespec tp1, tp2;
        struct timespec diff;

        // cout<< "Doing stat: " << endl;
        get_time(&tp1);
        do_stat(copy_info);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&stat_time, &diff);

        // cout<< "Doing src opens: " << endl;
        get_time(&tp1);
        do_src_opens(copy_info);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&src_open_time, &diff);


        // cout<< "Doing dst opens: " << endl;
        get_time(&tp1);
        do_dst_opens(copy_info);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&dst_open_time, &diff);

        // cout<< "Doing copies: " << endl;
        get_time(&tp1);
        do_data_copy_serial_linked(copy_info);
        get_time(&tp2);
        diff = get_diff(&tp1, &tp2);
        update_ts(&data_copy_time, &diff);
    }

    void do_create_dirs(vector<CopyJobInfo>& copy_info) {
        // cout<< "Doing create dest dirs: " << endl;

        struct io_uring_sqe *sqe;
        struct io_uring_cqe *cqe;

        assert(ring.num_cqe_pending_tasks() == 0);

        if(copy_info.size() == 0) return;

        for(int i=0; i<copy_info.size(); i++) {
            CopyJobInfo& info = copy_info[i];

            sqe = ring.get_sqe();
            //! TODO: Use the parent directory fd instead of -1 to
            // cout<< "Creating destination directory: " << info.dst << endl;
            io_uring_prep_mkdirat(sqe, -1, info.dst.c_str(), 0777);
            if(i != copy_info.size()-1){
                sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
            }
        }
        ring.submit_and_wait_nr(copy_info.size(), copy_info.size()-1);

        ring.get_cqe(&cqe);
        assert(cqe->res >= 0);
        ring.see_cqe(cqe);
    }

    void create_dest_dir() {
        // cout<< "Creating destination directory: " << dst_dir_path << endl;
        dst_dir_fd = mkdir(dst_dir_path.c_str(), 0777);
    }

    vector<array<filesystem::path, 2>> copy_dir_files() {
        create_dest_dir();
        if(!dir_fetched)
            fetch_dir();

        uint8_t *bufp;
        uint8_t *end;

        filesystem::path src_path, dst_path, dst_dir;

        bufp = (uint8_t *)&dirent_buf[0];
        end = bufp + dirent_buf.size();

        vector<array<filesystem::path, 2>> directories;
        vector<CopyJobInfo> copy_files;
        vector<CopyJobInfo> copy_dirs;

        while (bufp < end) {
            struct linux_dirent64 *dent;

            dent = (struct linux_dirent64 *)bufp;
            if (strcmp(dent->d_name, ".") && strcmp(dent->d_name, "..")) {
                // Create copy jobs;
                src_path = dir_path / dent->d_name;
                dst_path = this->dst_dir_path / dent->d_name;
                // cout<< "Read the following dentry: " << src_path << endl;
                if(dent->d_type == DT_REG) {
                    // copy the files
                    CopyJobInfo info(src_path, dst_path);
                    copy_files.push_back(info);
                } 
                else if (dent->d_type == DT_DIR) {
                    // create the directories
                    CopyJobInfo info(src_path, dst_path);
                    copy_dirs.push_back(info);
                    directories.push_back({src_path, dst_path});
                }
            }
            bufp += dent->d_reclen;
        }

        cout << "Num dirs: " << copy_dirs.size() << endl;

        do_copy_files(copy_files);
        do_create_dirs(copy_dirs);

        return directories;
    }
};

void get_time(struct timespec *tp) {
    int ret;
    int sec;
    long nsec;
    ret = clock_gettime(CLOCK_MONOTONIC, tp);
    tp->tv_sec += tp->tv_nsec / BILLION;
    tp->tv_nsec = tp->tv_nsec % BILLION;
    assert(ret == 0);
}

struct timespec get_diff(struct timespec *start, struct timespec *end) {
    struct timespec sp;
    sp.tv_nsec = end->tv_nsec - start->tv_nsec;
    sp.tv_sec = end->tv_sec - start->tv_sec;

    if(sp.tv_nsec < 0) {
        sp.tv_sec -= 1;
        sp.tv_nsec += BILLION;
    }

    return sp;
}

void update_ts(struct timespec *target, struct timespec *diff) {
    target->tv_nsec += diff->tv_nsec;
    target->tv_sec += diff->tv_sec;
}

void print_metric(struct timespec *ts, string metric) {
    cout << metric << ": " << ts->tv_sec << " seconds and " << ts->tv_nsec << " nanoseconds" << endl;
}

void print_all_metrics() {
    print_metric(&setup_time, "Setup");
    print_metric(&src_open_time, "Opening source files");
    print_metric(&stat_time, "Stat Time");
    print_metric(&dst_open_time, "Destination source files");
    print_metric(&data_copy_time, "Data Copy Time");
}

int main(int argc, char**argv) {
    struct timespec tp1, tp2;
    get_time(&tp1);
    IORing ring(RING_SIZE);
    get_time(&tp2);

    struct timespec diff = get_diff(&tp1, &tp2);

    update_ts(&setup_time, &diff);

    assert(argc == 3);


    queue<array<filesystem::path, 2>> q;
    filesystem::path src_path(argv[1]);
    filesystem::path dst_path(argv[2]);
    mkdir(dst_path.c_str(), 0777);

    cout << "src_path = " << src_path << endl;
    cout << "dst_path = " << dst_path << endl;

    vector<array<filesystem::path, 2>> dirs;
    array<filesystem::path, 2> dir_val;
    // vector<array<filesystem::path, 2>> new_dirs;

    DirJob job(src_path, dst_path, ring, DIR_BUF_SIZE);
    dirs = job.copy_dir_files();

    for(auto& dir: dirs) {
        q.push(dir);
    }

    while(q.size() > 0) {
        dir_val = q.front();

        for(auto& dir: DirJob(dir_val[0], dir_val[1], ring, DIR_BUF_SIZE).copy_dir_files()) {
            q.push(dir);
        }
        q.pop();
    }

    print_all_metrics();
}
