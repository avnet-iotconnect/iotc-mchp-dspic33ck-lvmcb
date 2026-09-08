#include <string.h>
#include "device_config.h"
#include "nvm_flash.h"

#define DEVICE_CONFIG_VERSION 1U

// NVM_FLASH_WriteBytes() writes 2 instruction-word slots (4 logical
// bytes) per call - see nvm_flash.h.
#define NVM_FLASH_DOUBLE_WORD_SIZE (2U * NVM_FLASH_WORD_SIZE)

static uint32_t CRC32_Compute(const uint8_t *data, uint16_t len)
{
    // Bitwise CRC-32 (polynomial 0xEDB88320) - no lookup table, to keep
    // flash usage small; this runs once per boot and once per provisioning
    // write, so speed does not matter here.
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

bool DEVICE_CONFIG_Load(device_config_t *out)
{
    NVM_FLASH_ReadBytes(0, (uint8_t *)out, sizeof(device_config_t));

    if (out->magic != DEVICE_CONFIG_MAGIC)
    {
        return false;
    }

    uint32_t stored_crc = out->crc32;
    out->crc32 = 0;
    uint32_t computed_crc = CRC32_Compute((const uint8_t *)out, sizeof(device_config_t));
    out->crc32 = stored_crc;

    return stored_crc == computed_crc;
}

bool DEVICE_CONFIG_Save(device_config_t *cfg)
{
    cfg->magic = DEVICE_CONFIG_MAGIC;
    cfg->version = DEVICE_CONFIG_VERSION;
    cfg->crc32 = 0;
    cfg->crc32 = CRC32_Compute((const uint8_t *)cfg, sizeof(device_config_t));

    if (!NVM_FLASH_ErasePage())
    {
        return false;
    }

    // NVM_FLASH_WriteBytes() requires a length that's a multiple of the
    // double-word write granularity; pad the tail with zeros in a scratch
    // buffer rather than requiring device_config_t itself to be pre-sized
    // to a multiple of it (the struct's layout may shift across future
    // field additions/compiler versions).
    uint16_t padded_len = (uint16_t)((sizeof(device_config_t) + (NVM_FLASH_DOUBLE_WORD_SIZE - 1U)) & ~(NVM_FLASH_DOUBLE_WORD_SIZE - 1U));
    uint8_t buf[((sizeof(device_config_t) + NVM_FLASH_DOUBLE_WORD_SIZE - 1U) / NVM_FLASH_DOUBLE_WORD_SIZE) * NVM_FLASH_DOUBLE_WORD_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, cfg, sizeof(device_config_t));

    return NVM_FLASH_WriteBytes(0, buf, padded_len);
}
