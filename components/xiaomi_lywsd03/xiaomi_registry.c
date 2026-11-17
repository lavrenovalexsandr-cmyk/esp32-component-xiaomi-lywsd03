#include "xiaomi_registry.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "XIAOMI_REG";
#define NVS_NAMESPACE "xiaomi"
#define NVS_KEY_COUNT "count"

typedef struct
{
    uint8_t mac[6];
    uint8_t key[XIAOMI_BIND_KEY_SIZE];
} xiaomi_registry_storage_entry_t;

static nvs_handle_t s_nvs_handle;
static bool s_initialized;
static SemaphoreHandle_t s_mutex;

static void secure_zero(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0) {
        *p++ = 0;
    }
}

static xiaomi_registry_result_t lock_registry(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return XIAOMI_REGISTRY_NOT_INITIALIZED;
    }
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE
        ? XIAOMI_REGISTRY_OK : XIAOMI_REGISTRY_BUSY;
}

static void unlock_registry(void)
{
    (void)xSemaphoreGive(s_mutex);
}

static void make_entry_key(size_t index, char *key, size_t key_size)
{
    (void)snprintf(key, key_size, "s%u", (unsigned)index);
}

static xiaomi_registry_result_t get_count_unlocked(size_t *count)
{
    if (count == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }

    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(s_nvs_handle, NVS_KEY_COUNT, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return XIAOMI_REGISTRY_OK;
    }
    if (err != ESP_OK || value > XIAOMI_REGISTRY_MAX_DEVICES) {
        ESP_LOGE(TAG, "Failed to read valid sensor count: %s",
                 esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    *count = value;
    return XIAOMI_REGISTRY_OK;
}

static xiaomi_registry_result_t set_count_unlocked(size_t count)
{
    if (count > XIAOMI_REGISTRY_MAX_DEVICES) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }

    esp_err_t err = nvs_set_u8(s_nvs_handle, NVS_KEY_COUNT, (uint8_t)count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sensor count: %s", esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }
    return XIAOMI_REGISTRY_OK;
}

static xiaomi_registry_result_t save_entry_unlocked(
    size_t index, const xiaomi_registry_entry_t *entry)
{
    if (entry == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }

    char key[16];
    make_entry_key(index, key, sizeof(key));

    xiaomi_registry_storage_entry_t storage;
    memcpy(storage.mac, entry->mac, sizeof(storage.mac));
    memcpy(storage.key, entry->key, sizeof(storage.key));

    esp_err_t err = nvs_set_blob(s_nvs_handle, key, &storage, sizeof(storage));
    secure_zero(&storage, sizeof(storage));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sensor entry %u: %s",
                 (unsigned)index, esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }
    return XIAOMI_REGISTRY_OK;
}

static xiaomi_registry_result_t load_entry_unlocked(
    size_t index, xiaomi_registry_entry_t *entry)
{
    if (entry == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }

    char key[16];
    make_entry_key(index, key, sizeof(key));

    xiaomi_registry_storage_entry_t storage;
    size_t size = sizeof(storage);
    esp_err_t err = nvs_get_blob(s_nvs_handle, key, &storage, &size);

    if (err != ESP_OK || size != sizeof(storage)) {
        secure_zero(&storage, sizeof(storage));
        ESP_LOGE(TAG, "Failed to read sensor entry %u: %s",
                 (unsigned)index, esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    memcpy(entry->mac, storage.mac, sizeof(entry->mac));
    memcpy(entry->key, storage.key, sizeof(entry->key));
    secure_zero(&storage, sizeof(storage));
    return XIAOMI_REGISTRY_OK;
}

static xiaomi_registry_result_t erase_entry_unlocked(size_t index)
{
    char key[16];
    make_entry_key(index, key, sizeof(key));

    esp_err_t err = nvs_erase_key(s_nvs_handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return XIAOMI_REGISTRY_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase sensor entry %u: %s",
                 (unsigned)index, esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }
    return XIAOMI_REGISTRY_OK;
}

xiaomi_registry_result_t xiaomi_registry_init(void)
{
    if (s_initialized) {
        return XIAOMI_REGISTRY_OK;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    size_t count = 0;
    xiaomi_registry_result_t result = get_count_unlocked(&count);
    if (result != XIAOMI_REGISTRY_OK) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
        return result;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Sensor registry ready: %u sensors", (unsigned)count);
    return XIAOMI_REGISTRY_OK;
}

xiaomi_registry_result_t xiaomi_registry_find_key(
    const uint8_t mac[6], uint8_t key[XIAOMI_BIND_KEY_SIZE])
{
    if (mac == NULL || key == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }
    xiaomi_registry_result_t lock_result = lock_registry();
    if (lock_result != XIAOMI_REGISTRY_OK) {
        return lock_result;
    }

    size_t count = 0;
    xiaomi_registry_result_t result = get_count_unlocked(&count);
    for (size_t i = 0; result == XIAOMI_REGISTRY_OK && i < count; i++) {
        xiaomi_registry_entry_t entry;
        result = load_entry_unlocked(i, &entry);
        if (result != XIAOMI_REGISTRY_OK) {
            secure_zero(&entry, sizeof(entry));
            break;
        }
        if (memcmp(entry.mac, mac, 6) == 0) {
            memcpy(key, entry.key, XIAOMI_BIND_KEY_SIZE);
            secure_zero(&entry, sizeof(entry));
            unlock_registry();
            return XIAOMI_REGISTRY_OK;
        }
        secure_zero(&entry, sizeof(entry));
    }

    unlock_registry();
    return result == XIAOMI_REGISTRY_OK ? XIAOMI_REGISTRY_NOT_FOUND : result;
}

xiaomi_registry_result_t xiaomi_registry_add(
    const uint8_t mac[6], const uint8_t key[XIAOMI_BIND_KEY_SIZE])
{
    if (mac == NULL || key == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }
    xiaomi_registry_result_t lock_result = lock_registry();
    if (lock_result != XIAOMI_REGISTRY_OK) {
        return lock_result;
    }

    size_t count = 0;
    xiaomi_registry_result_t result = get_count_unlocked(&count);
    for (size_t i = 0; result == XIAOMI_REGISTRY_OK && i < count; i++) {
        xiaomi_registry_entry_t entry;
        result = load_entry_unlocked(i, &entry);
        if (result != XIAOMI_REGISTRY_OK) {
            secure_zero(&entry, sizeof(entry));
            break;
        }
        bool exists = memcmp(entry.mac, mac, 6) == 0;
        secure_zero(&entry, sizeof(entry));
        if (exists) {
            result = XIAOMI_REGISTRY_ALREADY_EXISTS;
            break;
        }
    }

    if (result == XIAOMI_REGISTRY_OK && count >= XIAOMI_REGISTRY_MAX_DEVICES) {
        result = XIAOMI_REGISTRY_FULL;
    }

    if (result == XIAOMI_REGISTRY_OK) {
        xiaomi_registry_entry_t entry;
        memcpy(entry.mac, mac, sizeof(entry.mac));
        memcpy(entry.key, key, sizeof(entry.key));

        result = save_entry_unlocked(count, &entry);
        secure_zero(&entry, sizeof(entry));

        if (result == XIAOMI_REGISTRY_OK) {
            result = set_count_unlocked(count + 1);
        }
        if (result == XIAOMI_REGISTRY_OK) {
            esp_err_t err = nvs_commit(s_nvs_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
                result = XIAOMI_REGISTRY_STORAGE_ERROR;
            }
        }
    }

    unlock_registry();
    return result;
}

xiaomi_registry_result_t xiaomi_registry_remove(const uint8_t mac[6])
{
    if (mac == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }
    xiaomi_registry_result_t lock_result = lock_registry();
    if (lock_result != XIAOMI_REGISTRY_OK) {
        return lock_result;
    }

    size_t count = 0;
    xiaomi_registry_result_t result = get_count_unlocked(&count);
    size_t found = count;

    for (size_t i = 0; result == XIAOMI_REGISTRY_OK && i < count; i++) {
        xiaomi_registry_entry_t entry;
        result = load_entry_unlocked(i, &entry);
        if (result != XIAOMI_REGISTRY_OK) {
            secure_zero(&entry, sizeof(entry));
            break;
        }
        bool matches = memcmp(entry.mac, mac, 6) == 0;
        secure_zero(&entry, sizeof(entry));
        if (matches) {
            found = i;
            break;
        }
    }

    if (result == XIAOMI_REGISTRY_OK && found == count) {
        result = XIAOMI_REGISTRY_NOT_FOUND;
    }

    if (result == XIAOMI_REGISTRY_OK) {
        if (found != count - 1) {
            xiaomi_registry_entry_t last;
            result = load_entry_unlocked(count - 1, &last);
            if (result == XIAOMI_REGISTRY_OK) {
                result = save_entry_unlocked(found, &last);
            }
            secure_zero(&last, sizeof(last));
        }

        if (result == XIAOMI_REGISTRY_OK) {
            result = erase_entry_unlocked(count - 1);
        }
        if (result == XIAOMI_REGISTRY_OK) {
            result = set_count_unlocked(count - 1);
        }
        if (result == XIAOMI_REGISTRY_OK) {
            esp_err_t err = nvs_commit(s_nvs_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
                result = XIAOMI_REGISTRY_STORAGE_ERROR;
            }
        }
    }

    unlock_registry();
    return result;
}

xiaomi_registry_result_t xiaomi_registry_count(size_t *count)
{
    if (count == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }
    xiaomi_registry_result_t lock_result = lock_registry();
    if (lock_result != XIAOMI_REGISTRY_OK) {
        return lock_result;
    }
    xiaomi_registry_result_t result = get_count_unlocked(count);
    unlock_registry();
    return result;
}

xiaomi_registry_result_t xiaomi_registry_get(
    size_t index, xiaomi_registry_entry_t *entry)
{
    if (entry == NULL) {
        return XIAOMI_REGISTRY_INVALID_ARGUMENT;
    }
    xiaomi_registry_result_t lock_result = lock_registry();
    if (lock_result != XIAOMI_REGISTRY_OK) {
        return lock_result;
    }

    size_t count = 0;
    xiaomi_registry_result_t result = get_count_unlocked(&count);
    if (result == XIAOMI_REGISTRY_OK) {
        if (index >= count) {
            result = XIAOMI_REGISTRY_NOT_FOUND;
        } else {
            result = load_entry_unlocked(index, entry);
        }
    }

    unlock_registry();
    return result;
}
