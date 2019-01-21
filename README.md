# scan2d
Linux interface to SCX-4600 MFP (Multi-Function Printer) control panel buttons and menu.

## Use Case
Have a MFP connected to the router acting as print and scan server. 
MFP has buttons and I want to use them for a fast "computer free" scan.

## Files
* scan2menu.bin - used by scan2d.c is the customizable definition of choices that will be available from the MFP control panel.
  Its length must be 1118 bytes. Its format is clear by the example and comments in scan2.sh.
  Installed to /opt/etc.
* scan2.sh - used by scan2d.c is the customizable definition of actions for MFP buttons.
  Installed to /opt/etc.
* scan2d.c - a daemon managed by USB hotplug events. It works with printer kernel driver using /dev/usb/lp0 to poll the MFP.
  Binary compiled in [NSLU2-Linux](http://www.nslu2-linux.org/) optware-devel environment.
  Installed to /opt/sbin.
* hotplug - example of PnP used on DD-WRT OS configuration. 
  Installed to /sbin.

## Notes
Best USB sniffer - USBlyser; second - usb-monitor-pro.

## Compile
gcc -I/opt/include -O2 -W -Wall -Wl,-rpath,/opt/lib -o scan2d scan2d.c -lusb
