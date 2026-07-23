Jalali Digital Clock Applet
===========================

This plugin is based on the original LXDE 'Digital Clock Applet'.
It uses <lib/libjalali> to provide Jalali calendar format strings
for the Digital Clock applet.


Compilation
-----------

See <COMPILEME.c> for compilation instructions.
After compilation you will get <jdclock.so.so> file, copy it to your
appropriate LXDE panel plugin path, e.g., on Debian:
    `sudo cp jdclock.so /usr/lib/x86_64-linux-gnu/lxpanel/plugins/`
    `sudo cp jdclock.so /usr/lib/???/lxpanel/plugins/`

It will appear with the name 'Jalali Digital Clock' in LXDE panels:
'Add / Remove Panel Items'  ->  'Add'  ->  'Jalali Digital Clock'.


Usage
-----

Use `%d`, `%t`, ... for normal strftime formats.
Use `%JX` for Jalali version of strftime, it will get converted
to `%X` and passed to jstrftime() funtion again.
see: <libjalali/jtime.c> for more details. 


Example
-------

| Input:  `%d %b  -  %JD %JV`
| Output:  `17 Jul  -  ۲۷ تیر`

| Input:  `(%Jv = %JX)  (%JD = %Jd)  (%JZ = %JY)`
| Output:  `(۰۴ = تیر)  (26 = ۲۶)  (1405 = ۱۴۰۵)`
