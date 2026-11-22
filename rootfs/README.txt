Loop-OS initial ramdisk
=======================

Everything under rootfs/ in the source tree is packed into this archive by
`make initrd` and loaded by the bootloader as a module. The kernel serves it
read-only.

Try: ls /, ls /etc, cat /etc/motd
