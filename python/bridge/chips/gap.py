#
# Copyright (C) 2018 ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)

from bridge.default_debug_bridge import *
import time

JTAG_SOC_AXIREG = 4

JTAG_SOC_CONFREG = 7
JTAG_SOC_CONFREG_WIDTH = 4

BOOT_MODE_JTAG = 4
BOOT_MODE_JTAG_HYPER = 11
CONFREG_BOOT_WAIT = 1
CONFREG_PGM_LOADED = 1
CONFREG_INIT = 0

FEATURES={
    'target.xml':'''<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target>
  <architecture>riscv:rv32</architecture>

  <feature name="org.gnu.gdb.riscv.rv32i">
    <reg name="x0"  bitsize="32" group="general"/>
    <reg name="x1"  bitsize="32" group="general"/>
    <reg name="x2"  bitsize="32" group="general"/>
    <reg name="x3"  bitsize="32" group="general"/>
    <reg name="x4"  bitsize="32" group="general"/>
    <reg name="x5"  bitsize="32" group="general"/>
    <reg name="x6"  bitsize="32" group="general"/>
    <reg name="x7"  bitsize="32" group="general"/>
    <reg name="x8"  bitsize="32" group="general"/>
    <reg name="x9"  bitsize="32" group="general"/>
    <reg name="x10" bitsize="32" group="general"/>
    <reg name="x11" bitsize="32" group="general"/>
    <reg name="x12" bitsize="32" group="general"/>
    <reg name="x13" bitsize="32" group="general"/>
    <reg name="x14" bitsize="32" group="general"/>
    <reg name="x15" bitsize="32" group="general"/>
    <reg name="x16" bitsize="32" group="general"/>
    <reg name="x17" bitsize="32" group="general"/>
    <reg name="x18" bitsize="32" group="general"/>
    <reg name="x19" bitsize="32" group="general"/>
    <reg name="x20" bitsize="32" group="general"/>
    <reg name="x21" bitsize="32" group="general"/>
    <reg name="x22" bitsize="32" group="general"/>
    <reg name="x23" bitsize="32" group="general"/>
    <reg name="x24" bitsize="32" group="general"/>
    <reg name="x25" bitsize="32" group="general"/>
    <reg name="x26" bitsize="32" group="general"/>
    <reg name="x27" bitsize="32" group="general"/>
    <reg name="x28" bitsize="32" group="general"/>
    <reg name="x29" bitsize="32" group="general"/>
    <reg name="x30" bitsize="32" group="general"/>
    <reg name="x31" bitsize="32" group="general"/>
  </feature>
</target>
'''
}

class gap_debug_bridge(debug_bridge):

    def __init__(self, config, binaries=[], verbose=0, fimages=[]):
        super(gap_debug_bridge, self).__init__(config=config, binaries=binaries, verbose=verbose)

        self.fimages = fimages
        self.start_cores = False
        # self.capabilities("qXfer:features:read+")

    def stop(self):
        self.is_started = False
        # Reset the chip and tell him we want to load via jtag
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        self.log(1, "Notifying to boot code that we are doing a JTAG boot")
        if (not self.get_cable().chip_reset(True)):
            self.log(0, "Failed to assert chip reset")

        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)

        if (not self.get_cable().chip_reset(False)):
            self.log(0, "Failed to deassert chip reset")

        # Removed synchronization with boot code due to HW bug, it is better
        # to stop fc as soon as possible

#        # Now wait until the boot code tells us we can load the code
#        if self.verbose:
#            print ("Waiting for notification from boot code")
#        while True:
#            reg_value = self.get_cable().jtag_get_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
#            if reg_value == CONFREG_BOOT_WAIT:
#                break
#        print ("Received for notification from boot code")

        # Stall the FC
        self.write(0x1B300000, 4, [0, 0, 1, 0])

        # Configure FLL with no lock to avoid the HW bug with fll
        self.write_32(0x1a100004, 0x840005f5)
        self.write_32(0x1a100008, 0x8100410b)

        return True

    def qxfer_read(self, object, annex):
        if object != "features":
            raise XferInvalidObjectException()
        if annex not in FEATURES:
            raise XferInvalidAnnexException()
        return FEATURES[annex]

    def load_jtag(self):

        self.log(1, 'Loading binary through jtag')

        if not self.stop():
            return False

        # Load the binary through jtag

        for binary in self.binaries:
            self.log(1, "Loading binary from " + binary)
            if not self.load_elf(binary=binary):
                return False

        # Be careful to set the new PC only after the code is loaded as the prefetch
        # buffer is immediately fetching instructions and would get wrong instructions
        self.write(0x1B302000, 4, [0x80, 0x00, 0x00, 0x1c])

        self.start_cores = True
        return True


    def start(self):
        if self.start_cores and not self.is_started:
            self.log(1, 'Starting execution')

            self.is_started = True
            # Unstall the FC so that it starts fetching instructions from the loaded binary
            return self.write(0x1B300000, 4, [0, 0, 0, 0])

        return False


    def load_jtag_hyper(self):

        self.log(1, 'Loading binary through jtag_hyper')

        # Reset the chip and tell him we want to load from hyper
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        self.log(1, "Notifying to boot code that we are doing a JTAG boot from hyperflash")
        res = self.get_cable().chip_reset(True)
        res = res and self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG_HYPER)
        res = res and self.get_cable().chip_reset(False)

        return res


    def flash(self, f_path):
        MAX_BUFF_SIZE = (350*1024)
        addrHeader = self._get_binary_symbol_addr('flasherHeader')
        addrImgRdy = addrHeader
        addrFlasherRdy = addrHeader + 4
        addrFlashAddr = addrHeader + 8
        addrIterTime = addrHeader + 12
        addrBufSize = addrHeader + 16
        # open the file in read binary mode
        f_img = open(f_path, 'rb')
        f_size = os.path.getsize(f_path)
        lastSize = f_size % MAX_BUFF_SIZE;
        if(lastSize):
            n_iter = f_size // MAX_BUFF_SIZE + 1;
        else:
            n_iter = f_size // MAX_BUFF_SIZE

        flasher_ready = self.read_32(addrFlasherRdy)
        while(flasher_ready == 0):
            flasher_ready = self.read_32(addrFlasherRdy)
        flasher_ready = 0
        addrBuffer = self.read_32((addrHeader+20))
        self.log(1, "Flash address buffer 0x{:x}".format(addrBuffer))
        self.write_32(addrFlashAddr, 0)
        self.write_32(addrIterTime, n_iter)
        for i in range(n_iter):
            if (lastSize and i == (n_iter-1)):
                buff_data = f_img.read(lastSize)
                self.write(addrBuffer, lastSize, buff_data)
                self.write_32(addrBufSize, ((lastSize + 3) & ~3))
            else:
                buff_data = f_img.read(MAX_BUFF_SIZE)
                self.write(addrBuffer, MAX_BUFF_SIZE, buff_data)
                self.write_32(addrBufSize, MAX_BUFF_SIZE)
            self.write_32(addrImgRdy, 1)
            self.write_32(addrFlasherRdy, 0)
            if (i!=(n_iter-1)):
                flasher_ready = self.read_32(addrFlasherRdy)
                while(flasher_ready == 0):
                    flasher_ready = self.read_32(addrFlasherRdy)
        f_img.close()
        return True


    def load_jtag_old(self):

        self.log(1, 'Loading binary through jtag')

        # Reset the chip and tell him we want to load via jtag
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        self.log(1, "Notifying to boot code that we are doing a JTAG boot")
        self.get_cable().chip_reset(True)
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
        self.get_cable().chip_reset(False)

        # Now wait until the boot code tells us we can load the code
        self.log(1, "Waiting for notification from boot code")
        while True:
            reg_value = self.get_cable().jtag_get_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
            if reg_value == CONFREG_BOOT_WAIT:
                break
        self.log(1, "Received notification from boot code")


        # Load the binary through jtag
        self.log(1, "Loading binaries")
        for binary in self.binaries:
            if self.load_elf(binary=binary):
                return False

        return True


    def start_old(self):

        # And notify the boot code that the binary is ready
        self.log(1, "Notifying to boot code that the binary is loaded")
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, CONFREG_PGM_LOADED)

        return True


