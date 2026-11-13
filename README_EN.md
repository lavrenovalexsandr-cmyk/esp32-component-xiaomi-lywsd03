# ESP32 Xiaomi LYWSD03MMC

ESP-IDF component for Xiaomi LYWSD03MMC BLE temperature and humidity sensors.

The component handles MiBeacon detection, bind-key lookup, AES-CCM decryption, and measurement parsing. BLE scanning, MQTT, and Home Assistant integration remain outside this component.

## Dependencies

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble)
- ESP-IDF NVS and PSA Crypto

## Integration

Initialize NVS and `platform_ble` first, then initialize this component. Forward advertisements from the platform scan callback to `xiaomi_lywsd03_process_adv()`. Register a measurement callback with `xiaomi_lywsd03_set_callback()`.

## Public API

### Registering sensors

A bind key must be exactly 16 bytes. MAC addresses use normal display byte order.

- `xiaomi_lywsd03_add_sensor(mac, key)` adds a sensor or replaces its key if the MAC is already registered.
- `xiaomi_lywsd03_remove_sensor(mac)` removes a registered sensor.
- `xiaomi_lywsd03_has_key(mac)` checks whether a key is registered.
- `xiaomi_lywsd03_sensor_count()` returns the number of registered sensors.
- `xiaomi_lywsd03_get_sensor(index, &info)` returns the MAC for a zero-based registry index.

The public API does not expose bind keys for reading. The component uses them internally for decryption.

The registry is stored in the NVS namespace `xiaomi`, preserving compatibility with the tested implementation in `esp32_mqtt_xiaomi_lywsd03`. The application must initialize NVS before calling `xiaomi_lywsd03_init()`.

### Measurements

`xiaomi_lywsd03_data_t` contains the sensor MAC, temperature, humidity, battery level, and a validity flag for each measurement. A packet is accepted only after successful AES-CCM authentication and when at least one supported measurement is present.

The data callback runs synchronously inside `xiaomi_lywsd03_process_adv()`, in the caller's context. Keep it short and do not retain the data pointer after the callback returns.

## Responsibilities

- No BLE scan lifecycle management.
- No MQTT or Home Assistant dependencies.
- The application forwards advertisements to `xiaomi_lywsd03_process_adv()`.
- Bind-key input and transport to the ESP32 belong to the application/integration; this component provides registration and storage APIs.
