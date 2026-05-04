#ifndef __BLE_PROV_H__
#define __BLE_PROV_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_prov_init(void);
void ble_prov_start(void);
void ble_prov_stop(void);
bool ble_prov_is_active(void);
bool ble_prov_is_connected(void);
bool ble_prov_wait_connect(uint32_t timeout_ms);
bool ble_prov_wait_connected(uint32_t timeout_ms);
bool ble_prov_sleep_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLE_PROV_H__ */
