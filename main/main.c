#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "platform_ble.h"
#include "platform_ble_scan.h"
#include "xiaomi_lywsd03.h"

static const char *TAG = "xiaomi_example";

#define ADV_QUEUE_LENGTH 12
#define ADV_DATA_MAX_SIZE 255

typedef struct {
    platform_ble_adv_packet_t packet;
    uint8_t data[ADV_DATA_MAX_SIZE];
} queued_adv_t;

static QueueHandle_t s_adv_queue;

static void on_sensor_data(const xiaomi_lywsd03_data_t *data, void *context)
{
    (void)context;
    if (data == NULL) return;

    ESP_LOGI(TAG, "Sensor %02X:%02X:%02X:%02X:%02X:%02X",
             data->mac[0], data->mac[1], data->mac[2],
             data->mac[3], data->mac[4], data->mac[5]);

    if (data->temperature_valid) ESP_LOGI(TAG, "  Temperature: %.1f C", data->temperature);
    if (data->humidity_valid) ESP_LOGI(TAG, "  Humidity: %.1f %%", data->humidity);
    if (data->battery_valid) ESP_LOGI(TAG, "  Battery: %u %%", data->battery);
}

/* NimBLE host callback: copy matching reports to a queue; never do crypto here. */
static void on_ble_advertisement(const platform_ble_adv_packet_t *packet, void *context)
{
    (void)context;
    if (packet == NULL || packet->data == NULL ||
        packet->data_len == 0 || packet->data_len > ADV_DATA_MAX_SIZE) return;
    if (!xiaomi_lywsd03_is_packet(packet)) return;

    queued_adv_t item = {0};
    item.packet = *packet;
    memcpy(item.data, packet->data, packet->data_len);
    item.packet.data = item.data;
    (void)xQueueSend(s_adv_queue, &item, 0); /* Drop if full; do not block host. */
}

static void xiaomi_worker_task(void *arg)
{
    (void)arg;
    queued_adv_t item;
    for (;;) {
        if (xQueueReceive(s_adv_queue, &item, portMAX_DELAY) == pdTRUE) {
            item.packet.data = item.data;
            (void)xiaomi_lywsd03_process_adv(&item.packet);
        }
    }
}

void app_main(void)
{
    /* Never erase NVS automatically: the Xiaomi registry stores bind keys. */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(xiaomi_lywsd03_init());
    xiaomi_lywsd03_set_callback(on_sensor_data, NULL);

    s_adv_queue = xQueueCreate(ADV_QUEUE_LENGTH, sizeof(queued_adv_t));
    if (s_adv_queue == NULL) {
        ESP_LOGE(TAG, "Could not create BLE advertisement queue");
        return;
    }
    if (xTaskCreate(xiaomi_worker_task, "xiaomi_worker", 4096,
                    NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not create Xiaomi worker task");
        vQueueDelete(s_adv_queue);
        s_adv_queue = NULL;
        return;
    }

    ESP_ERROR_CHECK(platform_ble_init(NULL));
    ESP_ERROR_CHECK(platform_ble_scan_set_callback(on_ble_advertisement, NULL));
    ESP_ERROR_CHECK(platform_ble_scan_start(NULL));
    ESP_LOGI(TAG, "Xiaomi LYWSD03MMC example started");
}
