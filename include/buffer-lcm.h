#include <stddef.h>
#include <unordered_set>

class BufferManager
{
public:
    BufferManager(int num_bufs, size_t size);
    BufferManager();
    void init(int num_bufs, size_t size);
    char* get_next_buf();
    void free_buf(char* buf);
    void free_all();
private:
    int i_;
    int num_bufs_;
    std::unordered_set<char *> bufs_;
    size_t size_;
    size_t page_size_;
};

size_t buffer_lcm (const size_t, const size_t, const size_t);
