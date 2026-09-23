/** file: COMPILEME.c
    created on: 17 Jul 2026

  Compile Me file.
  Compilation:
    cc -Wall -Wextra -Werror \
       `pkg-config --cflags gtk+-2.0 lxpanel`  -I../lib/libjalali/ \
       -shared -fPIC COMPILEME.c -o jdclock.so \
       `pkg-config --libs lxpanel`  -Wl,-rpath,/usr/lib/x86_64-linux-gnu/lxpanel
 */
#include "jdclock.c"

#include "jtime.c"
#include "jalali.c"
