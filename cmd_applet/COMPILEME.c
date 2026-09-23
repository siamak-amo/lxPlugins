/** file: COMPILEME.c
    created on: 17 Jul 2026

  Compile Me file.
  Compilation:
    cc -Wall -Wextra -Werror \
       `pkg-config --cflags gtk+-2.0 lxpanel` \
       -shared -fPIC COMPILEME.c -o cmd_runner.so \
       `pkg-config --libs lxpanel` -Wl,-rpath,/usr/lib/x86_64-linux-gnu/lxpanel
 */
#include "cmd_runner.c"
#include "exec.c"
