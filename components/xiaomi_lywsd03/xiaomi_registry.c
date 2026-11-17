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

#define NVS_KEY_REMOVE_JOURNAL "rm_journal"
#define REMOVE_JOURNAL_MAGIC 0x584D524Au
#define REMOVE_JOURNAL_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint8_t version;
    uint8_t old_count;
    uint8_t found_index;
    uint8_t reserved;
    xiaomi_registry_storage_entry_t last_entry;
    uint32_t checksum;
} xiaomi_registry_remove_journal_t;

static bool s_faulted;

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
    if (s_faulted) {
        return XIAOMI_REGISTRY_STORAGE_ERROR;
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


static uint32_t journal_checksum(const xiaomi_registry_remove_journal_t *journal)
{
    const uint8_t *bytes = (const uint8_t *)journal;
    const size_t length = offsetof(xiaomi_registry_remove_journal_t, checksum);
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static xiaomi_registry_result_t apply_remove_journal_unlocked(
    const xiaomi_registry_remove_journal_t *journal)
{
    xiaomi_registry_result_t result = XIAOMI_REGISTRY_OK;

    if (journal->found_index != (uint8_t)(journal->old_count - 1u)) {
        xiaomi_registry_entry_t last;
        memcpy(last.mac, journal->last_entry.mac, sizeof(last.mac));
        memcpy(last.key, journal->last_entry.key, sizeof(last.key));
        result = save_entry_unlocked(journal->found_index, &last);
        secure_zero(&last, sizeof(last));
    }

    if (result == XIAOMI_REGISTRY_OK) {
        result = erase_entry_unlocked(journal->old_count - 1u);
    }
    if (result == XIAOMI_REGISTRY_OK) {
        result = set_count_unlocked((size_t)journal->old_count - 1u);
    }
    if (result == XIAOMI_REGISTRY_OK) {
        esp_err_t err = nvs_commit(s_nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS remove transaction commit failed: %s",
                     esp_err_to_name(err));
            result = XIAOMI_REGISTRY_STORAGE_ERROR;
        }
    }
    if (result == XIAOMI_REGISTRY_OK) {
        esp_err_t err = nvs_erase_key(s_nvs_handle, NVS_KEY_REMOVE_JOURNAL);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to clear remove journal: %s", esp_err_to_name(err));
            result = XIAOMI_REGISTRY_STORAGE_ERROR;
        }
    }
    if (result == XIAOMI_REGISTRY_OK) {
        esp_err_t err = nvs_commit(s_nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Remove journal cleanup commit failed: %s",
                     esp_err_to_name(err));
            result = XIAOMI_REGISTRY_STORAGE_ERROR;
        }
    }
    return result;
}

static xiaomi_registry_result_t recover_remove_journal_unlocked(void)
{
    xiaomi_registry_remove_journal_t journal;
    size_t size = sizeof(journal);
    esp_err_t err = nvs_get_blob(s_nvs_handle, NVS_KEY_REMOVE_JOURNAL, &journal, &size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return XIAOMI_REGISTRY_OK;
    }
    if (err != ESP_OK || size != sizeof(journal)) {
        secure_zero(&journal, sizeof(journal));
        ESP_LOGE(TAG, "Remove journal read failed or has invalid size");
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    bool valid = journal.magic == REMOVE_JOURNAL_MAGIC &&
                 journal.version == REMOVE_JOURNAL_VERSION &&
                 journal.old_count > 0 &&
                 journal.old_count <= XIAOMI_REGISTRY_MAX_DEVICES &&
                 journal.found_index < journal.old_count &&
                 journal.checksum == journal_checksum(&journal);
    if (!valid) {
        secure_zero(&journal, sizeof(journal));
        ESP_LOGE(TAG, "Remove journal is corrupt; refusing registry access");
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    ESP_LOGW(TAG, "Recovering interrupted sensor removal");
    xiaomi_registry_result_t result = apply_remove_journal_unlocked(&journal);
    secure_zero(&journal, sizeof(journal));
    return result;
}

xiaomi_registry_result_t xiaomi_registry_init(void)
{
    if (s_initialized) {
        return s_faulted ? XIAOMI_REGISTRY_STORAGE_ERROR : XIAOMI_REGISTRY_OK;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return XIAOMI_REGISTRY_STORAGE_ERROR;
    }

    xiaomi_registry_result_t result = recover_remove_journal_unlocked();
    if (result != XIAOMI_REGISTRY_OK) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
        return result;
    }

    size_t count = 0;
    result = get_count_unlocked(&count);
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
        xiaomi_registry_remove_journal_t journal;
        memset(&journal, 0, sizeof(journal));
        journal.magic = REMOVE_JOURNAL_MAGIC;
        journal.version = REMOVE_JOURNAL_VERSION;
        journal.old_count = (uint8_t)count;
        journal.found_index = (uint8_t)found;

        xiaomi_registry_entry_t last;
        result = load_entry_unlocked(count - 1u, &last);
        if (result == XIAOMI_REGISTRY_OK) {
            memcpy(journal.last_entry.mac, last.mac, sizeof(last.mac));
            memcpy(journal.last_entry.key, last.key, sizeof(last.key));
            journal.checksum = journal_checksum(&journal);

            esp_err_t err = nvs_set_blob(s_nvs_handle, NVS_KEY_REMOVE_JOURNAL,
                                         &journal, sizeof(journal));
            if (err == ESP_OK) {
                err = nvs_commit(s_nvs_handle);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to persist remove journal: %s", esp_err_to_name(err));
                // Commit failure can leave the durable state uncertain.
                s_faulted = true;
                result = XIAOMI_REGISTRY_STORAGE_ERROR;
            } else {
                result = apply_remove_journal_unlocked(&journal);
                if (result != XIAOMI_REGISTRY_OK) {
                    // Keep the journal for reboot recovery and block further access.
                    s_faulted = true;
                }
            }
        }
        secure_zero(&last, sizeof(last));
        secure_zero(&journal, sizeof(journal));
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
