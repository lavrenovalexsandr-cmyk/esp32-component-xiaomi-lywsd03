# ESP32 Xiaomi LYWSD03MMC

Компонент ESP-IDF для работы с BLE-термометрами Xiaomi LYWSD03MMC.

Компонент отвечает за распознавание MiBeacon, проверку bind key, AES-CCM-дешифровку и разбор измерений. BLE-сканирование, MQTT и Home Assistant находятся за пределами компонента.

## Зависимости

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble)
- NVS и PSA Crypto из ESP-IDF

## Подключение

Добавьте компонент в проект ESP-IDF и подключите зависимости. Инициализируйте NVS и BLE-платформу до использования драйвера.

```c
#include "platform_ble.h"
#include "platform_ble_scan.h"
#include "xiaomi_lywsd03.h"

static void on_sensor_data(
    const xiaomi_lywsd03_data_t *data,
    void *context)
{
    (void)context;
    // Обработка измерений датчика.
}

static void on_ble_advertisement(
    const platform_ble_adv_packet_t *packet,
    void *context)
{
    (void)context;
    xiaomi_lywsd03_process_adv(packet);
}

void app_start(void)
{
    // NVS и platform_ble должны быть инициализированы ранее.
    ESP_ERROR_CHECK(xiaomi_lywsd03_init());

    xiaomi_lywsd03_set_callback(on_sensor_data, NULL);
    ESP_ERROR_CHECK(platform_ble_scan_set_callback(
        on_ble_advertisement, NULL));
    ESP_ERROR_CHECK(platform_ble_scan_start(NULL));
}
```

## Регистрация датчика

Bind key — 16 байт. MAC передаётся в обычном порядке байтов.

```c
uint8_t mac[6] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6 };
uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE] = { /* 16 bytes */ };

bool added = xiaomi_lywsd03_add_sensor(mac, key);
```

Реестр хранится в NVS namespace `xiaomi`. Сохранённый формат совместим с проверенной реализацией из `esp32_mqtt_xiaomi_lywsd03`.

## Данные

Структура `xiaomi_lywsd03_data_t` содержит MAC, значения температуры, влажности и заряда батареи, а также отдельные флаги валидности каждого измерения.

Пакет считается успешно обработанным только после успешной AES-CCM-проверки и обнаружения хотя бы одного поддерживаемого измерения.

## Границы ответственности

- Компонент не запускает и не останавливает BLE-сканирование.
- Компонент не использует MQTT или Home Assistant.
- Передача рекламы выполняется приложением через `xiaomi_lywsd03_process_adv()`.
- NVS должна быть инициализирована приложением до `xiaomi_lywsd03_init()`.
