#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "xiaomi_lywsd03.h"

static const char *TAG = "xiaomi_nvs_test";

#define TEST_NAMESPACE "xly_test"
#define TEST_PHASE_KEY "phase"
#define TEST_BASELINE_KEY "baseline"

/*
 * Reserved locally administered MAC for this NVS test.
 * The test refuses to run if this MAC is already registered.
 */
static const uint8_t TEST_MAC[XIAOMI_LYWSD03_MAC_SIZE] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01
};

static const uint8_t TEST_KEY_1[XIAOMI_LYWSD03_BIND_KEY_SIZE] = {
    0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
    0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F
};

static const uint8_t TEST_KEY_2[XIAOMI_LYWSD03_BIND_KEY_SIZE] = {
    0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
    0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F
};

static void fail(const char *message)
{
    ESP_LOGE(TAG, "FAIL: %s", message);
}

static void reboot_for_next_phase(nvs_handle_t handle, uint8_t next_phase)
{
    ESP_ERROR_CHECK(nvs_set_u8(handle, TEST_PHASE_KEY, next_phase));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, "Phase complete. Restarting ESP32...");
    esp_restart();
}

static bool test_mac_is_registered(void)
{
    size_t count = xiaomi_lywsd03_sensor_count();

    for (size_t i = 0; i < count; i++) {
        xiaomi_lywsd03_sensor_info_t sensor = {0};

        if (!xiaomi_lywsd03_get_sensor(i, &sensor)) {
            fail("Could not enumerate sensor registry");
            return true;
        }

        if (memcmp(sensor.mac, TEST_MAC, sizeof(TEST_MAC)) == 0) {
            return true;
        }
    }

    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Xiaomi NVS persistence test");

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "nvs_flash_init failed: %s. No automatic erase performed.",
                 esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(xiaomi_lywsd03_init());

    nvs_handle_t test_handle;
    ESP_ERROR_CHECK(nvs_open(TEST_NAMESPACE, NVS_READWRITE, &test_handle));

    uint8_t phase = 0;
    err = nvs_get_u8(test_handle, TEST_PHASE_KEY, &phase);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        size_t baseline = xiaomi_lywsd03_sensor_count();

        if (baseline > UINT8_MAX) {
            fail("Unexpected sensor count");
            nvs_close(test_handle);
            return;
        }

        if (test_mac_is_registered()) {
            fail("Reserved test MAC is already registered; refusing to modify it");
            nvs_close(test_handle);
            return;
        }

        ESP_ERROR_CHECK(nvs_set_u8(test_handle, TEST_BASELINE_KEY,
                                   (uint8_t)baseline));
        ESP_ERROR_CHECK(nvs_set_u8(test_handle, TEST_PHASE_KEY, 0));
        ESP_ERROR_CHECK(nvs_commit(test_handle));
        phase = 0;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot read test phase: %s", esp_err_to_name(err));
        nvs_close(test_handle);
        return;
    }

    uint8_t baseline_u8 = 0;
    ESP_ERROR_CHECK(nvs_get_u8(test_handle, TEST_BASELINE_KEY, &baseline_u8));
    size_t expected_with_test_sensor = (size_t)baseline_u8 + 1;

    switch (phase) {
    case 0: {
        if (test_mac_is_registered()) {
            fail("Test MAC unexpectedly exists before test start");
            nvs_close(test_handle);
            return;
        }

        if (!xiaomi_lywsd03_add_sensor(TEST_MAC, TEST_KEY_1)) {
            fail("Adding test sensor failed");
            nvs_close(test_handle);
            return;
        }

        if (xiaomi_lywsd03_sensor_count() != expected_with_test_sensor ||
            !xiaomi_lywsd03_has_key(TEST_MAC)) {
            fail("Add sensor/count/key check failed");
            nvs_close(test_handle);
            return;
        }
        ESP_LOGI(TAG, "PASS: add sensor and verify registry count");

        if (!xiaomi_lywsd03_add_sensor(TEST_MAC, TEST_KEY_2)) {
            fail("Updating bind key failed");
            nvs_close(test_handle);
            return;
        }

        if (xiaomi_lywsd03_sensor_count() != expected_with_test_sensor) {
            fail("Updating existing sensor changed registry count");
            nvs_close(test_handle);
            return;
        }
        ESP_LOGI(TAG, "PASS: update bind key without duplicating sensor");
        reboot_for_next_phase(test_handle, 1);
        break;
    }

    case 1:
        if (!xiaomi_lywsd03_has_key(TEST_MAC) ||
            xiaomi_lywsd03_sensor_count() != expected_with_test_sensor ||
            !test_mac_is_registered()) {
            fail("Sensor was not restored correctly after reboot");
            nvs_close(test_handle);
            return;
        }
        ESP_LOGI(TAG, "PASS: sensor restored from NVS after reboot");

        if (!xiaomi_lywsd03_remove_sensor(TEST_MAC)) {
            fail("Removing test sensor failed");
            nvs_close(test_handle);
            return;
        }

        if (xiaomi_lywsd03_has_key(TEST_MAC) ||
            xiaomi_lywsd03_sensor_count() != baseline_u8) {
            fail("Sensor removal/count check failed");
            nvs_close(test_handle);
            return;
        }
        ESP_LOGI(TAG, "PASS: remove sensor and verify registry count");
        reboot_for_next_phase(test_handle, 2);
        break;

    case 2:
        if (xiaomi_lywsd03_has_key(TEST_MAC) ||
            test_mac_is_registered() ||
            xiaomi_lywsd03_sensor_count() != baseline_u8) {
            fail("Removed sensor reappeared after reboot");
            nvs_close(test_handle);
            return;
        }

        ESP_LOGI(TAG, "PASS: removal persisted after reboot");
        ESP_LOGI(TAG, "ALL NVS PERSISTENCE TESTS PASSED");

        ESP_ERROR_CHECK(nvs_erase_key(test_handle, TEST_PHASE_KEY));
        ESP_ERROR_CHECK(nvs_erase_key(test_handle, TEST_BASELINE_KEY));
        ESP_ERROR_CHECK(nvs_commit(test_handle));
        nvs_close(test_handle);
        ESP_LOGI(TAG, "Test state cleared. Xiaomi registry data was preserved.");
        break;

    default:
        ESP_LOGE(TAG, "Unknown test phase: %u", phase);
        nvs_close(test_handle);
        break;
    }
}
