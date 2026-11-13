#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "platform_ble.h"
#include "platform_ble_scan.h"
#include "xiaomi_lywsd03.h"

static const char *TAG = "xiaomi_test";

static void on_sensor_data(
    const xiaomi_lywsd03_data_t *data,
    void *context)
{
    (void)context;

    ESP_LOGI(TAG,
             "LYWSD03MMC %02X:%02X:%02X:%02X:%02X:%02X",
             data->mac[0], data->mac[1], data->mac[2],
             data->mac[3], data->mac[4], data->mac[5]);

    if (data->temperature_valid) {
        ESP_LOGI(TAG, "Temperature: %.1f C", data->temperature);
    }
    if (data->humidity_valid) {
        ESP_LOGI(TAG, "Humidity: %.1f %%", data->humidity);
    }
    if (data->battery_valid) {
        ESP_LOGI(TAG, "Battery: %u %%", data->battery);
    }
}

static void on_advertisement(
    const platform_ble_adv_packet_t *packet,
    void *context)
{
    (void)context;
    (void)xiaomi_lywsd03_process_adv(packet);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(platform_ble_init(NULL));
    ESP_ERROR_CHECK(xiaomi_lywsd03_init());

    xiaomi_lywsd03_set_callback(on_sensor_data, NULL);

    ESP_ERROR_CHECK(platform_ble_scan_set_callback(
        on_advertisement, NULL));
    ESP_ERROR_CHECK(platform_ble_scan_start(NULL));

    ESP_LOGI(TAG, "Xiaomi LYWSD03MMC test application started");
}
