#ifndef _FCP_H_
#define _FCP_H_

#include <iostream>
#include <string>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#define RINGSIZE 1024
#define REG_FD_SIZE 1024
#define IORING_OP_GETDENTS64 41
#define DIR_BUF_SIZE 16384

enum {
    FCP_OP_CREATDIR,
    FCP_OP_OPENDIR,
    FCP_OP_MKDIR,
    FCP_OP_GETDENTS,
    FCP_OP_OPENFILE,
    FCP_OP_READ,
    FCP_OP_WRITE,
    FCP_OP_CREATFILE,
    FCP_OP_STAT_COPY_JOB,
};

// States for copy job
// COPY_STAT_PENDING -> COPY_STAT_SUBMITTED, COPY_STAT_DONE -> COPY_CP_IN_PROGRESS -> COPY_CP_DONE
enum {
    COPY_STAT_PENDING,
    COPY_STAT_SUBMITTED,
    COPY_STAT_DONE,
    COPY_CP_IN_PROGRESS,
    COPY_CP_DONE
};


struct linux_dirent64 {
	int64_t		d_ino;    /* 64-bit inode number */
	int64_t		d_off;    /* 64-bit offset to next structure */
	unsigned short	d_reclen; /* Size of this dirent */
	unsigned char	d_type;   /* File type */
	char		d_name[]; /* Filename (null-terminated) */
};

class CopyJob;

class RequestMeta {
public:
    // TODO: Define getter/setter
    std::string dirpath;
    // to be used by readdir TODO: need better abstractions
    std::string dest_dirpath;
    int type;
    int reg_fd;
    CopyJob* cp_job;
    struct statx *statbuf;

    RequestMeta(int type) {
        this->type = type;
        this->reg_fd = -1;
        cp_job = NULL;
        statbuf = NULL;
    }
};


class CopyJob {
private:
    std::filesystem::path src;
    std::filesystem::path dst;
    std::string src_path;
    std::string dst_path;
    ssize_t size;
    ssize_t n_bytes_copied;
    int state;
    // file index
    int src_fd;
    int dst_fd;
    bool src_opened;
    bool dst_opened;
public:
    CopyJob(std::string src, std::string dst) {
        this->src = std::filesystem::path(src);
        this->dst = std::filesystem::path(dst);
        this->src_path = this->src.string();
        this->dst_path = this->dst.string();
        this->n_bytes_copied = 0;
        this->size = -1;
        this->state = COPY_STAT_PENDING;
        this->src_opened = false;
        this->dst_opened = false;
    }

    bool is_dst_opened() {
        return this->dst_opened;
    }

    bool is_src_opened() {
        return this->src_opened;
    }

    void set_src_opened() {
        this->src_opened = true;
    }

    void set_dst_opened() {
        this->dst_opened = true;
    }

    void set_src_fd(int fd) {
        this->src_fd = fd;
    }

    int get_src_fd() {
        return this->src_fd;
    }

    void set_dst_fd(int fd) {
        this->dst_fd = fd;
    }

    int get_dst_fd() {
        return this->dst_fd;
    }

    int get_state() {
        return this->state;
    }

    void set_state(int state) {
        this->state = state;
    }

    std::string& get_src_path() {
        return this->src_path;
    }

    std::string& get_dst_path() {
        return this->dst_path;
    }

    std::string get_dst_dir() {
        return this->dst.parent_path().string();
    }

    ssize_t get_size() {
        return this->size;
    }
    
    void set_size(ssize_t size) {
        std::cout << "Setting the size " << size << " for the file: " << this->dst_path << std::endl;
        this->size = size;
    }

    ssize_t get_bytes_copied() {
        return this->n_bytes_copied;
    }
    
    void add_bytes_copied(ssize_t num_bytes) {
        this->n_bytes_copied += num_bytes;
    }
};

// TODO: There's probably a better allocator for this
class RegFDAllocator {
private:
    int size;
    std::unordered_set<int> busy_list;
public:
    int *fd_list;
    // TODO: Write destructor.
    RegFDAllocator(int size) {
        this->size = size;
        fd_list = (int *)malloc(size * sizeof(int));
        for(int i=0; i<size; i++) {
            fd_list[i] = -1;
        }
    }

    int get_size() {
        return size;
    }

    // Returns -1 if full
    int get_free() {
        for(int i=0; i<size; i++) {
            if(busy_list.find(i) == busy_list.end()) {
                busy_list.insert(i);
                return i;
            }
        }
        std::cerr << "Failed to get a free filedescriptor, crashing " << std::endl;
        exit(1);
        return -1;
    }

    int release(int idx) {
        int ret;
        ret = busy_list.erase(idx);
        assert(ret == 1);

        return 0;
    }
};

#endif