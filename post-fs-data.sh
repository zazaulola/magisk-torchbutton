#!/system/bin/sh
# Runs in post-fs-data, before the daemon starts (service.sh runs in late_start).
#
# Android 13+ (especially Android 16 on Pixel/Tensor) ships a stricter SELinux
# policy: the magisk domain doesn't always have write access to the torch
# sysfs node or to /dev/uinput. We extend the live policy here.
#
# Failures are silent — magiskpolicy returns non-zero if a class/attribute
# doesn't exist on the current device, which is fine; the remaining rules
# still apply.

MODDIR=${0%/*}

magiskpolicy --live \
    'allow magisk sysfs_leds       file { read write open getattr ioctl }' \
    'allow magisk sysfs_camera     file { read write open getattr ioctl }' \
    'allow magisk sysfs            file { read write open getattr ioctl }' \
    'allow magisk uinput_device    chr_file { read write open getattr ioctl }' \
    'allow magisk input_device     chr_file { read write open getattr ioctl }' \
    2>/dev/null

# Some kernels label the torch node as sysfs_lights or sysfs_lcd_backlight
# instead — try those too.
magiskpolicy --live \
    'allow magisk sysfs_lights         file { read write open getattr ioctl }' \
    'allow magisk sysfs_lcd_backlight  file { read getattr open }' \
    'allow magisk sysfs_graphics       file { read getattr open }' \
    2>/dev/null

exit 0
