#include <stdio.h>

#if defined(__MINGW32__)
/* compatibility for older libraries looking for __iob_func in modern UCRT-based
 * MinGW */
void *__cdecl __imp___iob_func(void) {
  static FILE *_iob_shim[3];
  _iob_shim[0] = stdin;
  _iob_shim[1] = stdout;
  _iob_shim[2] = stderr;
  return (void *)_iob_shim;
}
#endif
