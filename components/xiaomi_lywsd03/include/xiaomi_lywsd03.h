#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform_ble_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XIAOMI_LYWSD03_MODEL "LYWSD03MMC"
#define XIAOMI_LYWSD03_MAC_SIZE 6
#define XIAOMI_LYWSD03_BIND_KEY_SIZE 16

typedef enum {
    XIAOMI_LYWSD03_OK = 0,
    XIAOMI_LYWSD03_NOT_FOUND,
    XIAOMI_LYWSD03_ALREADY_EXISTS,
    XIAOMI_LYWSD03_FULL,
    XIAOMI_LYWSD03_INVALID_ARGUMENT,
    XIAOMI_LYWSD03_NOT_INITIALIZED,
    XIAOMI_LYWSD03_BUSY,
    XIAOMI_LYWSD03_STORAGE_ERROR
} xiaomi_lywsd03_result_t;

typedef struct {
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE];
} xiaomi_lywsd03_sensor_info_t;

typedef struct {
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE];
    bool temperature_valid;
    bool humidity_valid;
    bool battery_valid;
    float temperature;
    float humidity;
    uint8_t battery;
} xiaomi_lywsd03_data_t;

typedef void (*xiaomi_lywsd03_data_callback_t)(
    const xiaomi_lywsd03_data_t *data,
    void *context);

esp_err_t xiaomi_lywsd03_init(void);
void xiaomi_lywsd03_set_callback(
    xiaomi_lywsd03_data_callback_t callback,
    void *context);
bool xiaomi_lywsd03_process_adv(const platform_ble_adv_packet_t *packet);
bool xiaomi_lywsd03_is_packet(const platform_ble_adv_packet_t *packet);
bool xiaomi_lywsd03_get_info(
    const platform_ble_adv_packet_t *packet,
    uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);

/*
 * Status-returning registry API. add_sensor_ex never replaces an existing key.
 * The legacy bool API below has the same safe, add-only behavior.
 */
xiaomi_lywsd03_result_t xiaomi_lywsd03_add_sensor_ex(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE],
    const uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE]);
xiaomi_lywsd03_result_t xiaomi_lywsd03_remove_sensor_ex(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
/** Clear all persisted Xiaomi sensor records and bind keys. */
xiaomi_lywsd03_result_t xiaomi_lywsd03_clear_registry(void);
xiaomi_lywsd03_result_t xiaomi_lywsd03_has_key_ex(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
xiaomi_lywsd03_result_t xiaomi_lywsd03_sensor_count_ex(size_t *count);
xiaomi_lywsd03_result_t xiaomi_lywsd03_get_sensor_ex(
    size_t index,
    xiaomi_lywsd03_sensor_info_t *sensor);

/* Compatibility wrappers. False/zero still collapse detailed error states. */
bool xiaomi_lywsd03_add_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE],
    const uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE]);
bool xiaomi_lywsd03_remove_sensor(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
bool xiaomi_lywsd03_has_key(
    const uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE]);
size_t xiaomi_lywsd03_sensor_count(void);
bool xiaomi_lywsd03_get_sensor(
    size_t index,
    xiaomi_lywsd03_sensor_info_t *sensor);

#ifdef __cplusplus
}
#endif
