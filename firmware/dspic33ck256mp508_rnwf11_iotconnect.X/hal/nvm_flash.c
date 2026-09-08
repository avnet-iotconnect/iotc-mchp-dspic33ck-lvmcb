#include <xc.h>
#include <string.h>
#include "nvm_flash.h"

// NVMCON.NVMOP values - dsPIC33CK256MP508 Family Flash Programming
// Specification (DS70005300G), Register 3-1's NVMOP<3:0> field, matching
// the worked page-erase (Table 3-6) and double-word-program (Table 3-7)
// examples.
#define NVMOP_PAGE_ERASE  0x3U
#define NVMOP_DOUBLE_WORD 0x1U

// Write latches always live at this fixed table-page/offset pair
// regardless of the real destination address (Section 3.8) - the real
// destination is set separately via NVMADR/NVMADRU below.
#define NVM_WRITE_LATCH_TBLPAG 0xFAU

static void NVM_FLASH_SetAddress(uint32_t addr)
{
    NVMADR = (uint16_t)(addr & 0xFFFFUL);
    NVMADRU = (uint16_t)(addr >> 16);
}

static bool NVM_FLASH_TriggerAndWait(uint16_t nvmop)
{
    NVMCONbits.WRERR = 0;
    NVMCONbits.WREN = 1;
    NVMCONbits.NVMOP = nvmop;

    // The NVMKEY unlock sequence must be immediately followed by setting
    // WR with nothing else executing in between (not even an ISR), or the
    // unlock is silently aborted - __builtin_disi() guarantees that
    // window, matching how Microchip's own reference drivers do this.
    __builtin_disi(5);
    NVMKEY = 0x55;
    NVMKEY = 0xAA;
    NVMCONbits.WR = 1;
    __builtin_nop();
    __builtin_nop();

    while (NVMCONbits.WR == 1)
    {
        // Self-timed by hardware; poll since we get here right after
        // setting WR.
    }
    bool ok = (NVMCONbits.WRERR == 0);
    NVMCONbits.WREN = 0;
    return ok;
}

bool NVM_FLASH_ErasePage(void)
{
    NVM_FLASH_SetAddress(NVM_FLASH_CONFIG_PAGE_ADDR);
    return NVM_FLASH_TriggerAndWait(NVMOP_PAGE_ERASE);
}

bool NVM_FLASH_WriteBytes(uint32_t offset, const uint8_t *data, uint16_t len)
{
    const uint16_t double_word_bytes = 2U * NVM_FLASH_WORD_SIZE;

    if ((offset % double_word_bytes) != 0U || (len % double_word_bytes) != 0U)
    {
        return false;
    }

    for (uint16_t written = 0; written < len; written += double_word_bytes)
    {
        uint16_t lsw0 = (uint16_t)data[written] | ((uint16_t)data[written + 1] << 8);
        uint16_t lsw1 = (uint16_t)data[written + 2] | ((uint16_t)data[written + 3] << 8);

        TBLPAG = NVM_WRITE_LATCH_TBLPAG;
        __builtin_tblwtl(0x0000, lsw0);
        __builtin_tblwth(0x0000, 0x0000); // upper "phantom" byte of each instruction word is unused
        __builtin_tblwtl(0x0002, lsw1);
        __builtin_tblwth(0x0002, 0x0000);

        // Each instruction-word slot is 2 PC-address units apart, and this
        // driver's logical-byte offset already advances 1:1 with PC-address
        // units (NVM_FLASH_WORD_SIZE bytes per slot == 2 PC units per slot).
        NVM_FLASH_SetAddress(NVM_FLASH_CONFIG_PAGE_ADDR + offset + written);

        if (!NVM_FLASH_TriggerAndWait(NVMOP_DOUBLE_WORD))
        {
            return false;
        }
    }
    return true;
}

void NVM_FLASH_ReadBytes(uint32_t offset, uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i += NVM_FLASH_WORD_SIZE)
    {
        uint32_t addr = NVM_FLASH_CONFIG_PAGE_ADDR + offset + i;
        TBLPAG = (uint16_t)(addr >> 16);
        uint16_t lsw = __builtin_tblrdl((uint16_t)(addr & 0xFFFFUL));
        data[i] = (uint8_t)(lsw & 0xFFU);
        if ((uint16_t)(i + 1U) < len)
        {
            data[i + 1U] = (uint8_t)(lsw >> 8);
        }
    }
}

bool NVM_FLASH_SelfTest(void)
{
    static const uint8_t pattern[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    uint8_t readback[sizeof(pattern)];

    if (!NVM_FLASH_ErasePage())
    {
        return false;
    }
    if (!NVM_FLASH_WriteBytes(0, pattern, sizeof(pattern)))
    {
        return false;
    }
    NVM_FLASH_ReadBytes(0, readback, sizeof(readback));
    bool matched = memcmp(pattern, readback, sizeof(pattern)) == 0;

    // Leave the page erased either way - a failed pattern write shouldn't
    // linger, and DEVICE_CONFIG_Save() will erase it again before the
    // first real provisioning write regardless.
    (void)NVM_FLASH_ErasePage();
    return matched;
}
