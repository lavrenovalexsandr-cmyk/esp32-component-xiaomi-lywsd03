# ESP32 Xiaomi LYWSD03MMC

ESP-IDF component for Xiaomi LYWSD03MMC BLE temperature and humidity sensors.

The component handles MiBeacon detection, bind-key lookup, AES-CCM authentication and decryption, and measurement parsing. BLE scanning, MQTT, and Home Assistant integration remain outside this component.

## Dependencies

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-component-ble)
- ESP-IDF NVS and PSA Crypto

## Integration

The esp32-platform-ble repository must be checked out next to this repository, or its location must be supplied through ESP32_PLATFORM_BLE_PATH.

```text
esp32/
├── esp32-platform-ble/
└── esp32_xiaomi_lywsd03/
```

The main application component in this repository is a minimal integration example. It initializes NVS, the Xiaomi driver and BLE, receives advertising reports, and forwards them to the driver.

### Initialization order

1. Initialize NVS.
2. Call xiaomi_lywsd03_init().
3. Register the measurement callback using xiaomi_lywsd03_set_callback().
4. Initialize BLE using platform_ble_init().
5. Set the scan callback and start scanning.

The application must initialize NVS before calling xiaomi_lywsd03_init(). The function returns ESP_OK on success and ESP_FAIL if registry initialization or PSA Crypto initialization fails. In particular, an internal registry recovery error (STORAGE_ERROR) is reported as ESP_FAIL by the public initialization function.

**Important:** do not automatically call nvs_flash_erase() when nvs_flash_init() fails in a production application. The NVS partition contains the sensor registry and bind keys; erasing it unconditionally would delete those keys.

The platform scan callback runs in NimBLE host context and must not block. The example copies advertising reports into a queue and performs AES-CCM processing in a separate worker task.

## Public API

### Initialization and BLE processing

```c
esp_err_t xiaomi_lywsd03_init(void);
void xiaomi_lywsd03_set_callback(
    xiaomi_lywsd03_data_callback_t callback, void *context);
bool xiaomi_lywsd03_process_adv(const platform_ble_adv_packet_t *packet);
bool xiaomi_lywsd03_is_packet(const platform_ble_adv_packet_t *packet);
bool xiaomi_lywsd03_get_info(
    const platform_ble_adv_packet_t *packet, uint8_t mac[6]);
```

The application owns the BLE scan lifecycle and forwards advertising reports to xiaomi_lywsd03_process_adv(). xiaomi_lywsd03_is_packet() identifies LYWSD03MMC advertisements, and xiaomi_lywsd03_get_info() extracts the MAC address from MiBeacon data.

### Sensor registry

A bind key must be exactly 16 bytes. MAC addresses use normal display byte order.

The registry capacity is **16 sensors**.

Use the status-returning API for registry operations:

```c
xiaomi_lywsd03_result_t xiaomi_lywsd03_add_sensor_ex(
    const uint8_t mac[6], const uint8_t key[16]);
xiaomi_lywsd03_result_t xiaomi_lywsd03_remove_sensor_ex(
    const uint8_t mac[6]);
xiaomi_lywsd03_result_t xiaomi_lywsd03_has_key_ex(
    const uint8_t mac[6]);
xiaomi_lywsd03_result_t xiaomi_lywsd03_sensor_count_ex(size_t *count);
xiaomi_lywsd03_result_t xiaomi_lywsd03_get_sensor_ex(
    size_t index, xiaomi_lywsd03_sensor_info_t *sensor);
```

Possible results are `OK`, `NOT_FOUND`, `ALREADY_EXISTS`, `FULL`, `INVALID_ARGUMENT`, `NOT_INITIALIZED`, `BUSY`, and `STORAGE_ERROR`.

- `add_sensor_ex()` returns `ALREADY_EXISTS` if the MAC is already registered; it never replaces an existing key.
- `remove_sensor_ex()` returns `NOT_FOUND` if the sensor is not registered.
- `has_key_ex()` returns `OK` when a key is found and `NOT_FOUND` when the MAC is not registered.
- `sensor_count_ex()` writes the number of registered sensors to the supplied pointer.
- `get_sensor_ex()` returns the sensor MAC at the requested index; bind keys are never exposed through the public API.

To replace a key, the application must explicitly remove the sensor and then add it again. This prevents duplicate MQTT delivery from silently overwriting a stored key.

Legacy bool/size_t functions remain available for source compatibility, but collapse detailed errors into false or zero. New integrations should use the `_ex` API.

Registry operations are protected by a mutex. A failed read of an existing entry is treated as a storage error, never as a missing sensor. Enumeration errors must not be interpreted as an empty registry.

The registry is stored in the NVS namespace `xiaomi`. The existing entry storage format is preserved. Sensor removal uses an NVS recovery journal: an interrupted removal is completed during the next registry initialization. If the journal is corrupt or recovery cannot complete, the internal registry initializer returns `STORAGE_ERROR`; the public `xiaomi_lywsd03_init()` function reports this as `ESP_FAIL`, and the component must not be used.

Bind keys are used internally for decryption and cannot be read through the public API. Temporary key and plaintext buffers are cleared after use.

### Measurements

`xiaomi_lywsd03_data_t` contains the sensor MAC, temperature, humidity, and battery level, with a separate validity flag for each measurement.

The data callback runs synchronously inside `xiaomi_lywsd03_process_adv()`, in the caller's task context. Do not retain the data pointer after the callback returns.

Supported MiBeacon object IDs:

| Object ID | Data |
|---|---|
| 0x1004 | Temperature |
| 0x1006 | Humidity |
| 0x100A | Battery level |
| 0x100D | Temperature and humidity |

A packet is accepted only after successful AES-CCM authentication and when at least one supported measurement is present.

## Responsibilities

- The component does not manage the BLE scan lifecycle.
- The component has no MQTT or Home Assistant dependency.
- The application receives BLE advertisements and forwards reports to the component.
- Bind-key input and transport to the ESP32 belong to the application/integration. This component provides the registration and storage API.
