# ESP32 Xiaomi LYWSD03MMC

ESP-IDF component for Xiaomi LYWSD03MMC BLE temperature and humidity sensors.

The component handles MiBeacon detection, bind-key lookup, AES-CCM authentication and decryption, and measurement parsing. BLE scanning, MQTT, and Home Assistant integration remain outside this component.

## Dependencies

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble)
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

**Important:** do not automatically call nvs_flash_erase() when nvs_flash_init() fails in a production application. The NVS partition contains the sensor registry and bind keys; erasing it unconditionally would delete those keys.

The platform scan callback runs in NimBLE host context and must not block. The example copies advertising reports into a queue and performs AES-CCM processing in a separate worker task.

## Public API

### Initialization

```c
esp_err_t xiaomi_lywsd03_init(void);
void xiaomi_lywsd03_set_callback(xiaomi_lywsd03_data_callback_t callback, void *context);
```

The application must initialize NVS before calling xiaomi_lywsd03_init().

### Sensor registration

A bind key must be exactly 16 bytes. MAC addresses use normal display byte order.

```c
uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE] = { /* 16-byte bind key */ };
bool added = xiaomi_lywsd03_add_sensor(mac, key);
```

If the MAC is already registered, xiaomi_lywsd03_add_sensor() replaces its stored key. Use xiaomi_lywsd03_remove_sensor() to remove a sensor.

Other available functions:
- xiaomi_lywsd03_has_key(mac) — check whether a key is registered;
- xiaomi_lywsd03_sensor_count() — get the number of registered sensors;
- xiaomi_lywsd03_get_sensor(index, &info) — get a sensor MAC by index;
- xiaomi_lywsd03_is_packet(packet) — identify LYWSD03MMC advertisements;
- xiaomi_lywsd03_get_info(packet, mac) — extract the MAC from MiBeacon;
- xiaomi_lywsd03_process_adv(packet) — process an advertising report.

The public API deliberately does not expose bind keys for reading. Keys are used internally for decryption.

The registry is stored in the NVS namespace xiaomi. Its storage format is compatible with the tested implementation in [esp32_mqtt_xiaomi_lywsd03](https://github.com/lavrenovalexsandr-cmyk/esp32_mqtt_xiaomi_lywsd03).

### Measurements

xiaomi_lywsd03_data_t contains the sensor MAC, temperature, humidity, and battery level, with a separate validity flag for each measurement.

The data callback runs synchronously inside xiaomi_lywsd03_process_adv(), in the caller's task context. Do not retain the data pointer after the callback returns.

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
- The application forwards advertising reports to xiaomi_lywsd03_process_adv().
- Bind-key input and transport to the ESP32 belong to the application/integration. This component provides the registration and storage API.