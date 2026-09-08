/**
 * @file provisioning.h
 * @brief Line-based provisioning protocol over UART1 (the debug console),
 *        driven by tools/provision_device_config.py/.ps1 - the same
 *        scripts and wire protocol the main dsPIC33AK512MPS512 branch
 *        uses, unmodified.
 *
 * Wire format (all lines end \r\n, matched case-sensitively):
 *   PC sends "PROVISION", then one "KEY=VALUE" line per device_config_t
 *   field (see provisioning.c for the exact key names), then "END".
 *   The device replies "OK" once the config is saved to flash, or
 *   "ERROR:<reason>" if a line couldn't be parsed or the save failed.
 */
#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdbool.h>
#include "../hal/device_config.h"

/**
 * @brief Blocks waiting for a complete PROVISION...END exchange on UART1.
 * @param out Filled in and saved to flash on success.
 * @return true if a valid config was received and saved.
 */
bool PROVISIONING_RunOnce(device_config_t *out);

/**
 * @brief Non-blocking check for the start of a provisioning exchange (the
 *        literal line "PROVISION"), used so the main loop can offer
 *        re-provisioning without ever blocking the motor-control loop.
 * @return true if "PROVISION" was just seen (caller should follow with
 *         PROVISIONING_RunOnce() to consume the rest of the exchange).
 */
bool PROVISIONING_CheckForRequest(void);

#endif // PROVISIONING_H
