Command Output Applet
=====================

This is a simple applet to show the output of the given commands,
periodically, in a given time interval.


Compilation & Usage
-------------------

See <COMPILEME.c> for compilation instructions.
After compilation you will get <cmd_runner.so> file, copy it to your
appropriate LXDE panel plugin path, e.g., on Debian:
    `sudo cp cmd_runner.so /usr/lib/x86_64-linux-gnu/lxpanel/plugins/`
    `sudo cp cmd_runner.so /usr/lib/???/lxpanel/plugins/`

Then it will appear with the name 'Command output' in LXDE panels:
'Add / Remove Panel Items'  ->  'Add'  ->  'Command output'.
