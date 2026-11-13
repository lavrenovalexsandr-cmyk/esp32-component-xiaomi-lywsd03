# ESP32 Xiaomi LYWSD03MMC

ESP-IDF-компонент для работы с BLE-датчиками Xiaomi LYWSD03MMC.

Компонент отвечает за распознавание MiBeacon, поиск bind-key, AES-CCM-аутентификацию и расшифровку, а также разбор измерений. BLE-сканирование, MQTT и Home Assistant находятся за пределами компонента.

## Зависимости

- ESP-IDF 5.x
- [esp32-platform-ble](https://github.com/lavrenovalexsandr-cmyk/esp32-platform-ble)
- NVS и PSA Crypto из ESP-IDF

## Подключение

Репозиторий esp32-platform-ble должен быть доступен рядом с этим репозиторием либо путь к нему нужно передать через ESP32_PLATFORM_BLE_PATH.

```text
esp32/
├── esp32-platform-ble/
└── esp32_xiaomi_lywsd03/
```

Компонент main в репозитории содержит минимальный пример: инициализация NVS, Xiaomi-драйвера и BLE, приём рекламных пакетов и передача их в драйвер.

### Порядок инициализации

1. Инициализировать NVS.
2. Вызвать xiaomi_lywsd03_init().
3. Установить callback измерений через xiaomi_lywsd03_set_callback().
4. Инициализировать BLE через platform_ble_init().
5. Установить callback сканирования и запустить сканирование.

**Важно:** не вызывайте nvs_flash_erase() автоматически при ошибке nvs_flash_init() в рабочем приложении. В NVS хранится реестр датчиков и bind-key; безусловное стирание приведёт к потере ключей.

BLE callback платформы выполняется в контексте NimBLE host и не должен блокироваться. В примере рекламные пакеты копируются в очередь, а AES-CCM и обработка измерений выполняются в отдельной задаче.

## Публичный API

### Инициализация

```c
esp_err_t xiaomi_lywsd03_init(void);
void xiaomi_lywsd03_set_callback(xiaomi_lywsd03_data_callback_t callback, void *context);
```

Приложение обязано инициализировать NVS до вызова xiaomi_lywsd03_init().

### Регистрация датчика

Bind-key должен содержать ровно 16 байт. MAC передаётся в обычном порядке байтов, как при записи адреса.

```c
uint8_t mac[XIAOMI_LYWSD03_MAC_SIZE] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
uint8_t key[XIAOMI_LYWSD03_BIND_KEY_SIZE] = { /* 16 байт bind-key */ };
bool added = xiaomi_lywsd03_add_sensor(mac, key);
```

Если MAC уже зарегистрирован, xiaomi_lywsd03_add_sensor() заменяет сохранённый ключ. Удаление выполняется через xiaomi_lywsd03_remove_sensor().

Другие функции API:
- xiaomi_lywsd03_has_key(mac) — проверить наличие ключа;
- xiaomi_lywsd03_sensor_count() — получить количество датчиков;
- xiaomi_lywsd03_get_sensor(index, &info) — получить MAC по индексу;
- xiaomi_lywsd03_is_packet(packet) — проверить, относится ли реклама к LYWSD03MMC;
- xiaomi_lywsd03_get_info(packet, mac) — извлечь MAC из MiBeacon;
- xiaomi_lywsd03_process_adv(packet) — обработать рекламный пакет.

Публичный API намеренно не предоставляет чтение bind-key обратно. Ключ используется внутри компонента для расшифровки.

Реестр хранится в NVS namespace xiaomi. Формат хранения совместим с проверенной реализацией из [esp32_mqtt_xiaomi_lywsd03](https://github.com/lavrenovalexsandr-cmyk/esp32_mqtt_xiaomi_lywsd03).

### Измерения

Структура xiaomi_lywsd03_data_t содержит MAC, температуру, влажность и заряд батареи. Для каждого измерения предусмотрен отдельный флаг валидности.

Callback вызывается синхронно из xiaomi_lywsd03_process_adv() в контексте вызывающей задачи. Не сохраняйте указатель на структуру после возврата из callback.

Поддерживаемые MiBeacon object ID:

| Object ID | Данные |
|---|---|
| 0x1004 | Температура |
| 0x1006 | Влажность |
| 0x100A | Заряд батареи |
| 0x100D | Температура и влажность |

Пакет считается успешно обработанным только после успешной AES-CCM-аутентификации и обнаружения хотя бы одного поддерживаемого измерения.

## Границы ответственности

- Компонент не запускает и не останавливает BLE-сканирование.
- Компонент не зависит от MQTT или Home Assistant.
- Приложение передаёт рекламные пакеты через xiaomi_lywsd03_process_adv().
- Ввод bind-key и доставка ключа на ESP32 относятся к приложению и интеграции. Компонент предоставляет API регистрации и хранения.