# ESP32 Xiaomi LYWSD03MMC

ESP-IDF component for Xiaomi LYWSD03MMC BLE temperature and humidity sensors.

The component handles MiBeacon detection, bind-key lookup, AES-CCM decryption, and measurement parsing. BLE scanning, MQTT, and Home Assistant integration remain outside this component.

## Dependencies

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble)
- ESP-IDF NVS and PSA Crypto

## Integration

Initialize NVS and `platform_ble` first, then initialize this component. Forward advertisements from the platform scan callback to `xiaomi_lywsd03_process_adv()`. Register a measurement callback with `xiaomi_lywsd03_set_callback()`.

See the Russian [README](README.md) for a complete integration example and API details.

## Registry

Bind keys are 16 bytes. MAC addresses use normal byte order. Sensor keys are stored in the NVS namespace `xiaomi`, preserving compatibility with the tested implementation in `esp32_mqtt_xiaomi_lywsd03`.

## Responsibilities

- No BLE scan lifecycle management.
- No MQTT or Home Assistant dependencies.
- Advertisement processing is explicitly invoked by the application.
- NVS must be initialized before `xiaomi_lywsd03_init()`.
