#include "buffer-lcm.h"

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
