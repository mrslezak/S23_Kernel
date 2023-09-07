# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=Custom Kernel for S23 series by MattoftheDead
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=dm1q
device.name2=dm2q
device.name3=dm3q
device.name4=
device.name5=
supported.versions=13
supported.patchlevels=2023-08 -
'; } # end properties

# shell variables
block=boot;
is_slot_device=0;
ramdisk_compression=auto;
patch_vbmeta_flag=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh && attributes;

## Trim partitions
$bin/busybox fstrim -v /data;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
set_perm_recursive 0 0 750 750 $ramdisk/*;

## AnyKernel boot install
flash_generic boot;
flash_generic vendor_boot;
flash_generic init_boot;
#flash_generic vendor_dlkm;
#flash_generic dtbo;
## end boot install
