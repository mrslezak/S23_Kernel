# Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 and
# only version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.


from parser_util import register_parser, RamParser, cleanupString
from mmu import Armv8MMU
from print_out import print_out_str
import struct
from parsers.properties import Properties

@register_parser('--logcat', 'Extract logcat logs from ramdump ')
class Logcat(RamParser):
    LOGCAT_BIN = "logcat.bin"
    def __init__(self, *args):
        super(Logcat, self).__init__(*args)
        self.f_path_offset = self.ramdump.field_offset('struct file', 'f_path')
        self.dentry_offset = self.ramdump.field_offset('struct path', 'dentry')
        self.d_iname_offset = self.ramdump.field_offset('struct dentry', 'd_iname')
        self.limit_size = int("0x20000000", 16)
        self.vma_list = []

    def swap64(self, val):
        return struct.unpack("<Q", struct.pack(">Q", val))[0]

    def empty_vma_list(self):
        for i in range(len(self.vma_list)):
            del self.vma_list[0]
        return

    def find_mmap_pgd(self):
        offset_comm = self.ramdump.field_offset('struct task_struct', 'comm')
        mm_offset = self.ramdump.field_offset('struct task_struct', 'mm')
        pgd = None
        mmap = None
        logd_task = None

        for task in self.ramdump.for_each_process():
            task_name = task + offset_comm
            task_name = cleanupString(self.ramdump.read_cstring(task_name, 16))
            if task_name == 'logd':
                mm_addr = self.ramdump.read_word(task + mm_offset)
                mmap = self.ramdump.read_structure_field(mm_addr, 'struct mm_struct',
                                                   'mmap')
                pgd = self.ramdump.read_structure_field(mm_addr, 'struct mm_struct',
                                                   'pgd')
                logd_task = task
                break

        return mmap, pgd, logd_task

    def get_logd_cnt_and_addr(self, logdmap):
        logdcount = 0
        logdaddr = 0

        # 3 logd vm_area, then heap
        # 4 logd + 1 bss, then heap in case of Android Q
        while logdmap != 0:
            tmpstartVm = self.ramdump.read_structure_field(
                            logdmap, 'struct vm_area_struct', 'vm_start')
            file = self.ramdump.read_structure_field(
                            logdmap, 'struct vm_area_struct', 'vm_file')

            if file != 0:
                dentry = self.ramdump.read_word(file + self.f_path_offset +
                                           self.dentry_offset)
                file_name = cleanupString(self.ramdump.read_cstring(
                                        dentry + self.d_iname_offset, 16))
                if file_name == "logd":
                    logdcount = logdcount + 1
                    if logdcount == 1:
                        logdaddr = self.ramdump.read_structure_field(
                              logdmap, 'struct vm_area_struct', 'vm_start')
                        logdaddr = (logdaddr & 0xFFFF000000) >> 0x18
            elif logdaddr != 0:
                checkaddr = tmpstartVm >> 0x18
                if checkaddr == logdaddr:
                    logdcount = logdcount + 1
            logdmap = self.ramdump.read_structure_field(
                               logdmap, 'struct vm_area_struct', 'vm_next')

        if logdcount < 3:
            print_out_str("found logd region {0} is smaller than expected. set " \
                  "logdcount to 3".format(logdcount))
            logdcount = 3

        return logdcount, logdaddr

    def flattened_range(self, mmap, logdcount, logdaddr):
        min = self.ramdump.read_structure_field(
                               mmap, 'struct vm_area_struct', 'vm_start')

        max = 0
        count = 0
        vma_info = {}

        while mmap != 0:
            tmpstartVm = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_start')
            if count == logdcount:
                min = tmpstartVm
                count = count + 1  # do not enter here again

            file = self.ramdump.read_structure_field(
                            mmap, 'struct vm_area_struct', 'vm_file')
            if file != 0:
                dentry = self.ramdump.read_word(file + self.f_path_offset +
                                           self.dentry_offset)
                file_name = cleanupString(self.ramdump.read_cstring(
                                        dentry + self.d_iname_offset, 16))
                if file_name == "logd":
                    count = count + 1
                if file_name.find("linker") == 0:
                    va_end = self.ramdump.read_structure_field(
                                   mmap, 'struct vm_area_struct', 'vm_end')
                    if va_end > max:
                        max = va_end
            elif logdaddr != 0:
                checkaddr = (tmpstartVm) >> 0x18
                if checkaddr == logdaddr:
                    count = count + 1
            mmap = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_next')
        size = max - min
        if size > self.limit_size:
            size = self.limit_size

        vma_info['header'] = None
        vma_info['start'] = min
        vma_info['size'] = size
        self.vma_list.append(vma_info)

        return

    def scattered_range(self, mmap, logdcount, logdaddr):
        min = 0
        max = 0
        count = 0
        prev_vmstart = 0
        prev_vmend = 0
        total_size = 0
        store_offset = 0
        meta_size = 32
        magic = 0xCECEC0DE

        while mmap != 0:
            vm_start = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_start')
            vm_end = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_end')
            vm_flags = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_flags')
            vm_file = self.ramdump.read_structure_field(
                            mmap, 'struct vm_area_struct', 'vm_file')

            set_min = False
            file_name = None
            if vm_file != 0:
                dentry = self.ramdump.read_word(vm_file + self.f_path_offset +
                                               self.dentry_offset)
                file_name = cleanupString(self.ramdump.read_cstring(
                                            dentry + self.d_iname_offset, 16))
            if count == logdcount:
                if vm_file != 0 or vm_flags & 0x3 != 0x3:
                    mmap = self.ramdump.read_structure_field(
                                        mmap, 'struct vm_area_struct', 'vm_next')
                    continue
                min = vm_start
                count = count + 1  # do not enter here again
            elif count < logdcount:
                if file_name == "logd":
                    count = count + 1
                elif logdaddr != 0:
                    checkaddr = (vm_start) >> 0x18
                    if checkaddr == logdaddr:
                        count = count + 1
            else:
                if min == 0:
                    if vm_file == 0 and vm_flags & 0x3 == 0x3:
                        min = vm_start
                    mmap = self.ramdump.read_structure_field(
                                        mmap, 'struct vm_area_struct', 'vm_next')
                    if mmap != 0:
                        prev_vmstart = vm_start
                        prev_vmend = vm_end
                    continue

                if vm_file != 0 or vm_flags & 0x3 != 0x3:
                    max = prev_vmend
                elif prev_vmend != 0 and prev_vmend != vm_start:
                    max = prev_vmend
                    set_min = True
                elif prev_vmend != 0 and prev_vmend == vm_start:
                    max = 0

            mmap = self.ramdump.read_structure_field(
                                mmap, 'struct vm_area_struct', 'vm_next')
            if mmap != 0:
                prev_vmstart = vm_start
                prev_vmend = vm_end

            if min != 0 and max != 0 and min < max:
                vma_info = {}
                size = max - min
                total_size = total_size + size
                vma_info['header'] = "{0:016x}{1:016x}{2:016x}{3:016x}".format(
                                          self.swap64(magic), self.swap64(min), self.swap64(size),
                                          self.swap64(store_offset + meta_size))
                vma_info['start'] = min
                vma_info['size'] = size
                self.vma_list.append(vma_info)
                store_offset = store_offset + size + meta_size

                min = 0
                max = 0
                if set_min is True:
                    min = vm_start

            if mmap == 0: #last
                if vm_start != 0 and prev_vmend != vm_start and vm_file == 0 and vm_flags & 0x3 == 0x3:
                    min = vm_start
                    max = vm_end
                elif vm_start != 0 and prev_vmend == vm_start:
                    max = vm_end

                if min != 0 and max != 0 and min < max:
                    vma_info = {}
                    size = max - min
                    total_size = total_size + size
                    vma_info['header'] = "{0:016x}{1:016x}{2:016x}{3:016x}".format(
                                              self.swap64(magic), self.swap64(min), self.swap64(size),
                                              self.swap64(store_offset + meta_size))
                    vma_info['start'] = min
                    vma_info['size'] = size
                    self.vma_list.append(vma_info)
                    store_offset = store_offset + size + meta_size

        if total_size > self.limit_size:
            print_out_str("size({0:d}) is too big".format(total_size))
            self.empty_vma_list()

        return

    def get_range(self, mmap, logdcount, logdaddr):
        if self.ramdump.arm64:
            self.scattered_range(mmap, logdcount, logdaddr)
        else:
            self.flattened_range(mmap, logdcount, logdaddr)
        return

    def generate_bin(self, mmu):
        self.ramdump.remove_file(self.LOGCAT_BIN)
        if len(self.vma_list) == 0:
            print_out_str("Failed to generate "+self.LOGCAT_BIN)
        else:
            print_out_str(self.LOGCAT_BIN+" base address is {0:x}".format(self.vma_list[0]['start']))
            with self.ramdump.open_file(self.LOGCAT_BIN, 'ab') as out_file:
                for vma_info in self.vma_list:
                    min = vma_info['start']
                    size = vma_info['size']
                    header = vma_info['header']
                    if header is not None:
                        header = bytearray.fromhex(header)
                        out_file.write(header)
                    max = min + size
                    while(min < max):
                        phys = mmu.virt_to_phys(min)
                        if phys is None:
                            min = min + 0x1000
                            out_file.write(b'\x00' * 0x1000)
                            continue

                        out_file.write(self.ramdump.read_physical(phys, 0x1000))
                        min = min + 0x1000
        return

    def parse(self):
        try:
            mmap, pgd, logd_task = self.find_mmap_pgd()
            if mmap is None:
                return
            pgdp = self.ramdump.virt_to_phys(pgd)
            mmu = Armv8MMU(self.ramdump, pgdp)
            propertyParser = Properties(self.ramdump)
            try:
                ver = int(propertyParser.find_property_from_file(mmu, mmap,
                        "ro.build.version.sdk","u:object_r:build_prop:s0"))
            except:
                ver = -1

            if not ver or ver == -1: #secondary prop
                try:
                    ver = int(propertyParser.find_property_from_file(mmu, mmap,
                            "ro.vndk.version","u:object_r:vndk_prop:s0"))
                except:
                    ver = -1
            print_out_str("Current sdk version is "+ str(ver))
            if ver >= 31: # Android S
                from parsers.logcat_v3 import Logcat_v3
                logcat = Logcat_v3(self.ramdump, mmu, logd_task)
                is_success = False
                try:
                    is_success = logcat.parse()
                except:
                    is_success = False
                if not is_success:
                    logdcount, logdaddr = self.get_logd_cnt_and_addr(mmap)
                    self.get_range(mmap, logdcount, logdaddr)
                    self.generate_bin(mmu)
                    from parsers.logcat_v3 import Logcat_vma
                    logcat = Logcat_vma(self.ramdump, mmu, self.LOGCAT_BIN)
                    logcat.parse()
            else:
                logdcount, logdaddr = self.get_logd_cnt_and_addr(mmap)
                self.get_range(mmap, logdcount, logdaddr)
                self.generate_bin(mmu)
        except Exception as result:
            print_out_str(str(result))

