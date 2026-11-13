#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "xiaomi_lywsd03.h"

static const char *TAG = "xiaomi_crypto_test";

#define TEST_NAMESPACE "xly_crypto_test"
#define TEST_PHASE_KEY "phase"

static const uint8_t TEST_MAC[XIAOMI_LYWSD03_MAC_SIZE] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6
};

/* Synthetic test key. It is not a real sensor's bind-key. */
static const uint8_t TEST_KEY[XIAOMI_LYWSD03_BIND_KEY_SIZE] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

/*
 * Deterministic MiBeacon test vector:
 * MAC:         A1:B2:C3:D4:E5:F6
 * Temperature: 23.4 C
 * Humidity:    56.7 %
 * Battery:     88 %
 *
 * AES-CCM parameters match the driver's implementation:
 * nonce = reversed MAC + product ID + frame counter + ext counter
 * AAD = 0x11, MIC = 4 bytes.
 */
static const uint8_t TEST_ADV[] = {
    0x02, 0x01, 0x06, 0x23, 0x16, 0x95, 0xFE,
    0x08, 0x00, 0x5B, 0x05, 0x2A, 0xF6, 0xE5, 0xD4, 0xC3,
    0xB2, 0xA1, 0x02, 0x1C, 0xE9, 0xE5, 0x2C, 0x20, 0x91,
    0x32, 0x6F, 0x2A, 0x6C, 0x8F, 0x1E, 0xD3, 0x01, 0x02,
    0x03, 0xA7, 0x73, 0xAA, 0xC8
};

typedef struct {
    bool called;
    xiaomi_lywsd03_data_t data;
} callback_result_t;

static callback_result_t s_callback_result;

static void on_sensor_data(
    const xiaomi_lywsd03_data_t *data,
    void *context)
{
    callback_result_t *result = context;
    if (data == NULL || result == NULL) {
        return;
    }

    result->called = true;
    result->data = *data;
}

static void fail(const char *message)
{
    ESP_LOGE(TAG, "FAIL: %s", message);
}

static void reboot_for_next_phase(nvs_handle_t handle)
{
    ESP_ERROR_CHECK(nvs_set_u8(handle, TEST_PHASE_KEY, 1));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "Test key stored. Rebooting to verify NVS restore...");
    esp_restart();
}

static bool verify_decoded_data(void)
{
    const xiaomi_lywsd03_data_t *data = &s_callback_result.data;

    if (!s_callback_result.called) {
        fail("Data callback was not called");
        return false;
    }

    if (memcmp(data->mac, TEST_MAC, sizeof(TEST_MAC)) != 0) {
        fail("Decoded MAC does not match test vector");
        return false;
    }

    if (!data->temperature_valid ||
        fabsf(data->temperature - 23.4f) > 0.05f) {
        fail("Temperature mismatch");
        return false;
    }

    if (!data->humidity_valid ||
        fabsf(data->humidity - 56.7f) > 0.05f) {
        fail("Humidity mismatch");
        return false;
    }

    if (!data->battery_valid || data->battery != 88) {
        fail("Battery mismatch");
        return false;
    }

    ESP_LOGI(TAG, "PASS: MAC decoded");
    ESP_LOGI(TAG, "PASS: temperature = %.1f C", data->temperature);
    ESP_LOGI(TAG, "PASS: humidity = %.1f %%", data->humidity);
    ESP_LOGI(TAG, "PASS: battery = %u %%", data->battery);
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting deterministic MiBeacon AES-CCM test");

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(xiaomi_lywsd03_init());

    nvs_handle_t test_handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NAMESPACE, NVS_READWRITE, &test_handle));

    uint8_t phase = 0;
    err = nvs_get_u8(test_handle, TEST_PHASE_KEY, &phase);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (xiaomi_lywsd03_has_key(TEST_MAC)) {
            fail("Test MAC is already registered; refusing to overwrite it");
            nvs_close(test_handle);
            return;
        }

        if (!xiaomi_lywsd03_add_sensor(TEST_MAC, TEST_KEY)) {
            fail("Could not register synthetic test sensor");
            nvs_close(test_handle);
            return;
        }

        ESP_LOGI(TAG, "PASS: synthetic test key saved to Xiaomi NVS");
        reboot_for_next_phase(test_handle);
        return;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read test phase: %s", esp_err_to_name(err));
        nvs_close(test_handle);
        return;
    }

    if (phase != 1) {
        ESP_LOGE(TAG, "Unknown test phase: %u", phase);
        nvs_close(test_handle);
        return;
    }

    if (!xiaomi_lywsd03_has_key(TEST_MAC)) {
        fail("Test key was not restored from NVS after reboot");
        nvs_close(test_handle);
        return;
    }
    ESP_LOGI(TAG, "PASS: bind-key restored from NVS after reboot");

    xiaomi_lywsd03_set_callback(on_sensor_data, &s_callback_result);

    platform_ble_adv_packet_t packet = {0};
    packet.data = TEST_ADV;
    packet.data_len = sizeof(TEST_ADV);

    if (!xiaomi_lywsd03_is_packet(&packet)) {
        fail("Test advertisement was not recognized as LYWSD03MMC");
        nvs_close(test_handle);
        return;
    }
    ESP_LOGI(TAG, "PASS: MiBeacon advertisement recognized");

    if (!xiaomi_lywsd03_process_adv(&packet) || !verify_decoded_data()) {
        fail("Valid encrypted test packet was not decoded");
        nvs_close(test_handle);
        return;
    }
    ESP_LOGI(TAG, "PASS: AES-CCM authentication and decryption");

    uint8_t tampered_adv[sizeof(TEST_ADV)];
    memcpy(tampered_adv, TEST_ADV, sizeof(TEST_ADV));
    tampered_adv[sizeof(tampered_adv) - 1] ^= 0x01;

    packet.data = tampered_adv;
    s_callback_result.called = false;

    if (xiaomi_lywsd03_process_adv(&packet) || s_callback_result.called) {
        fail("Tampered packet was incorrectly accepted");
        nvs_close(test_handle);
        return;
    }
    ESP_LOGI(TAG, "PASS: tampered MIC rejected");

    xiaomi_lywsd03_set_callback(NULL, NULL);

    if (!xiaomi_lywsd03_remove_sensor(TEST_MAC) ||
        xiaomi_lywsd03_has_key(TEST_MAC)) {
        fail("Could not remove synthetic test sensor");
        nvs_close(test_handle);
        return;
    }
    ESP_LOGI(TAG, "PASS: synthetic test sensor removed");

    ESP_ERROR_CHECK(nvs_erase_key(test_handle, TEST_PHASE_KEY));
    ESP_ERROR_CHECK(nvs_commit(test_handle));
    nvs_close(test_handle);

    ESP_LOGI(TAG, "ALL MIBEACON CRYPTO TESTS PASSED");
    ESP_LOGI(TAG, "Test state cleared; Xiaomi registry preserved.");
}
