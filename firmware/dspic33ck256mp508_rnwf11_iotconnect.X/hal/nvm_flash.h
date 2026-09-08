/**
 * @file nvm_flash.h
 * @brief Minimal on-chip flash double-word-program/page-erase driver for
 *        the dsPIC33CK256MP508.
 *
 * Hand-written directly from Microchip's "dsPIC33CK256MP508 Family Flash
 * Programming Specification" (DS70005300G) - Section 3.4 (NVMCON operation
 * codes, Register 3-1 / Tables 3-2 & 3-3), Section 3.7 (Page Erase, Table
 * 3-6) and Section 3.8 (Writing Code Memory, Table 3-7). That document
 * describes the ICSP programming flow, but the underlying NVMCON/NVMKEY/
 * TBLWTL-TBLWTH sequence it documents is the same mechanism this driver
 * uses to self-program from running application code.
 *
 * IMPORTANT - please validate on real hardware before relying on this:
 * this device's write path (TBLPAG=0xFA write-latch loading, Double-Word
 * Program via NVMCON=0x4001) is architecturally different from both the
 * sibling dsPIC33AK512MPS512 project's driver (memory-mapped NVMDATA0-3,
 * no unlock key) and the row-programming/NVMSRCADDR mechanism this driver
 * was originally planned around before the Programming Specification was
 * found - see nvm_flash.c for the verified sequence. A boot-time self-test
 * (write/read back a known pattern) is wired up in app code specifically
 * to catch a wrong assumption here quickly.
 *
 * Each 24-bit instruction word only stores 2 usable bytes through this
 * driver's API (the upper "phantom" byte of the packed instruction-word
 * format is left unused, matching how table-write instructions in
 * assembly address it) - see NVM_FLASH_WORD_SIZE.
 */
#ifndef NVM_FLASH_H
#define NVM_FLASH_H

#include <stdbool.h>
#include <stdint.h>

// Erase-page-aligned page reserved just below the device configuration
// (fuse) words at 0x2BF00, via -mreserve=prog@0x2B000:0x2B7FE (see the
// "oXC16ld-extra-opts" property in bldc.X/nbproject/configurations.xml) -
// this keeps the linker from ever placing code/data in this page, on this
// build or any future one. Confirmed working: the .const section, which
// previously landed right at the top of flash overlapping this range,
// moved down to make room once the reservation was added.
#define NVM_FLASH_CONFIG_PAGE_ADDR 0x2B000UL

// Usable capacity through this driver's API: 1024 instruction words per
// erase page, 2 usable bytes per word (see the file header comment).
#define NVM_FLASH_PAGE_CAPACITY 2048U

// Bytes stored per instruction-word "slot" - both NVM_FLASH_WriteBytes()'s
// offset/len and NVM_FLASH_ReadBytes()'s offset must be multiples of this.
#define NVM_FLASH_WORD_SIZE 2U

/**
 * @brief Erases the reserved page at NVM_FLASH_CONFIG_PAGE_ADDR.
 * @return true on success (NVMCON.WRERR == 0 after the erase completes).
 */
bool NVM_FLASH_ErasePage(void);

/**
 * @brief Double-word-programs (4 logical bytes / 2 instruction-word slots
 *        at a time) a buffer into the config page.
 * @param offset  Logical-byte offset from NVM_FLASH_CONFIG_PAGE_ADDR, must
 *                be a multiple of 2*NVM_FLASH_WORD_SIZE (4).
 * @param data    Buffer to write.
 * @param len     Length in logical bytes, must be a multiple of
 *                2*NVM_FLASH_WORD_SIZE (4).
 * @return true on success.
 * @note The target page must already be erased (all 1s) - call
 *       NVM_FLASH_ErasePage() first when overwriting existing data.
 */
bool NVM_FLASH_WriteBytes(uint32_t offset, const uint8_t *data, uint16_t len);

/**
 * @brief Reads back logical bytes previously written by NVM_FLASH_WriteBytes()
 *        (via TBLRDL - each instruction word's upper "phantom" byte is
 *        skipped, not included in the output).
 * @param offset  Logical-byte offset, must be a multiple of NVM_FLASH_WORD_SIZE.
 * @param len     Length in logical bytes.
 */
void NVM_FLASH_ReadBytes(uint32_t offset, uint8_t *data, uint16_t len);

/**
 * @brief Erases the page and writes/reads back a known test pattern, to
 *        confirm this driver's erase/write/read sequence actually works on
 *        real silicon (see the IMPORTANT note above) before trusting real
 *        provisioning data to it.
 * @return true if the pattern read back exactly as written.
 * @note Destructive - only call this when NVM_FLASH_ReadBytes()/
 *       DEVICE_CONFIG_Load() has already established the page holds no
 *       valid provisioned config, so there is nothing to lose.
 */
bool NVM_FLASH_SelfTest(void);

#endif // NVM_FLASH_H
