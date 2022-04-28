#include "buffer-lcm.h"
#include <unistd.h>
#include <stdlib.h>
#include <cassert>
/* Return a buffer size suitable for doing I/O with files whose block
   sizes are A and B.  However, never return a value greater than
   LCM_MAX.  */

size_t
buffer_lcm (const size_t a, const size_t b, const size_t lcm_max)
{
  size_t size;

  /* Use reasonable values if buffer sizes are zero.  */
  if (!a)
    size = b ? b : 8 * 1024;
  else
    {
      if (b)
        {
          /* Return lcm (A, B) if it is in range; otherwise, fall back
             on A.  */

          size_t lcm, m, n, q, r;

          /* N = gcd (A, B).  */
          for (m = a, n = b;  (r = m % n) != 0;  m = n, n = r)
            continue;

          /* LCM = lcm (A, B), if in range.  */
          q = a / n;
          lcm = q * b;
          if (lcm <= lcm_max && lcm / b == q)
            return lcm;
        }

      size = a;
    }

  return size <= lcm_max ? size : lcm_max;
}


BufferManager::BufferManager(int num_bufs, size_t size): num_bufs_(num_bufs), size_(size), i_(0)
{
  page_size_ = getpagesize();
  bufs_.reserve(num_bufs_);
}

BufferManager::BufferManager(): i_(0)
{
  page_size_ = getpagesize();
}

void BufferManager::init(int num_bufs, size_t size)
{
  num_bufs_ = num_bufs;
  size_ = size;
  bufs_.reserve(num_bufs_);
}

char* BufferManager::get_next_buf()
{
  if (i_ == num_bufs_) return NULL;

  char* buf = (char*)(malloc(size_));
  bufs_.insert(buf);
  i_++;
  return buf;
}

void BufferManager::free_buf(char* buf)
{
  auto it = bufs_.find(buf);
  if (it == bufs_.end()) return;
  free(buf);
  i_--;
  bufs_.erase(it);
  assert(i_ >= 0);
}

void BufferManager::free_all()
{
  for (auto ptr : bufs_)
  {
    free(ptr);
  }
  bufs_.clear();
  i_ = 0;
}