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

class gap_debug_bridge(debug_bridge):

    def __init__(self, config, binaries=[], verbose=False):
        super(gap_debug_bridge, self).__init__(config=config, binaries=binaries, verbose=verbose)

        self.start_cores = False


    def load_jtag(self):

        if self.verbose:
            print ('Loading binary through jtag')

        # Reset the chip and tell him we want to load via jtag
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        if self.verbose:
            print ("Notifying to boot code that we are doing a JTAG boot")
        self.get_cable().chip_reset(True)
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
        self.get_cable().chip_reset(False)

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

        # Load the binary through jtag
        if self.verbose:
            print ("Loading binaries")
        for binary in self.binaries:
            if self.load_elf(binary=binary):
                return 1

        # Be careful to set the new PC only after the code is loaded as the prefetch
        # buffer is immediately fetching instructions and would get wrong instructions
        self.write(0x1B302000, 4, [0x80, 0x00, 0x00, 0x1c])

        self.start_cores = True

        return 0


    def start(self):

        if self.start_cores:
            print ('Starting execution')

            # Unstall the FC so that it starts fetching instructions from the loaded binary
            self.write(0x1B300000, 4, [0, 0, 0, 0])

        return 0


    def load_jtag_hyper(self):

        if self.verbose:
            print ('Loading binary through jtag_hyper')

        # Reset the chip and tell him we want to load from hyper
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        if self.verbose:
            print ("Notifying to boot code that we are doing a JTAG boot from hyperflash")
        self.get_cable().chip_reset(True)
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG_HYPER)
        self.get_cable().chip_reset(False)

        return 0



    def load_jtag_old(self):

        if self.verbose:
            print ('Loading binary through jtag')

        # Reset the chip and tell him we want to load via jtag
        # We keep the reset active until the end so that it sees
        # the boot mode as soon as it boots from rom
        if self.verbose:
            print ("Notifying to boot code that we are doing a JTAG boot")
        self.get_cable().chip_reset(True)
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
        self.get_cable().chip_reset(False)

        # Now wait until the boot code tells us we can load the code
        if self.verbose:
            print ("Waiting for notification from boot code")
        while True:
            reg_value = self.get_cable().jtag_get_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, BOOT_MODE_JTAG)
            if reg_value == CONFREG_BOOT_WAIT:
                break
        print ("Received for notification from boot code")


        # Load the binary through jtag
        if self.verbose:
            print ("Loading binaries")
        for binary in self.binaries:
            if self.load_elf(binary=binary):
                return 1

        return 0


    def start_old(self):

        # And notify the boot code that the binary is ready
        if self.verbose:
            print ("Notifying to boot code that the binary is loaded")
        self.get_cable().jtag_set_reg(JTAG_SOC_CONFREG, JTAG_SOC_CONFREG_WIDTH, CONFREG_PGM_LOADED)

        return 0
