#include "xiaomi_registry.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"


static const char *TAG = "XIAOMI_REG";


#define NVS_NAMESPACE "xiaomi"
#define NVS_KEY_COUNT "count"


typedef struct
{
    uint8_t mac[6];

    uint8_t key[XIAOMI_BIND_KEY_SIZE];

} xiaomi_registry_storage_entry_t;


static nvs_handle_t s_nvs_handle = 0;

static bool s_initialized = false;


/* -------------------------------------------------------------------------- */
/* NVS helpers                                                                */
/* -------------------------------------------------------------------------- */

static void make_entry_key(
        size_t index,
        char *key,
        size_t key_size)
{
    snprintf(
        key,
        key_size,
        "s%u",
        (unsigned)index);
}


static bool get_count(
        size_t *count)
{
    if(count == NULL)
    {
        return false;
    }

    uint8_t value = 0;

    esp_err_t err =
        nvs_get_u8(
            s_nvs_handle,
            NVS_KEY_COUNT,
            &value);

    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        *count = 0;
        return true;
    }

    if(err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to read sensor count: %s",
            esp_err_to_name(err));

        return false;
    }

    if(value > XIAOMI_REGISTRY_MAX_DEVICES)
    {
        ESP_LOGE(
            TAG,
            "Invalid sensor count: %u",
            value);

        return false;
    }

    *count = value;

    return true;
}


static bool set_count(
        size_t count)
{
    if(count > XIAOMI_REGISTRY_MAX_DEVICES)
    {
        return false;
    }

    esp_err_t err =
        nvs_set_u8(
            s_nvs_handle,
            NVS_KEY_COUNT,
            (uint8_t)count);

    if(err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to save sensor count: %s",
            esp_err_to_name(err));

        return false;
    }

    return true;
}


static bool save_entry(
        size_t index,
        const xiaomi_registry_entry_t *entry)
{
    if(entry == NULL)
    {
        return false;
    }

    char key[16];

    make_entry_key(
        index,
        key,
        sizeof(key));


    xiaomi_registry_storage_entry_t storage;

    memcpy(
        storage.mac,
        entry->mac,
        sizeof(storage.mac));

    memcpy(
        storage.key,
        entry->key,
        sizeof(storage.key));


    esp_err_t err =
        nvs_set_blob(
            s_nvs_handle,
            key,
            &storage,
            sizeof(storage));

    if(err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to save sensor %u: %s",
            (unsigned)index,
            esp_err_to_name(err));

        return false;
    }

    return true;
}


static bool load_entry(
        size_t index,
        xiaomi_registry_entry_t *entry)
{
    if(entry == NULL)
    {
        return false;
    }

    char key[16];

    make_entry_key(
        index,
        key,
        sizeof(key));


    xiaomi_registry_storage_entry_t storage;

    size_t size =
        sizeof(storage);


    esp_err_t err =
        nvs_get_blob(
            s_nvs_handle,
            key,
            &storage,
            &size);


    if(err != ESP_OK)
    {
        return false;
    }


    if(size != sizeof(storage))
    {
        ESP_LOGE(
            TAG,
            "Invalid sensor entry size: index=%u size=%u",
            (unsigned)index,
            (unsigned)size);

        return false;
    }


    memcpy(
        entry->mac,
        storage.mac,
        sizeof(entry->mac));

    memcpy(
        entry->key,
        storage.key,
        sizeof(entry->key));


    return true;
}


static bool erase_entry(
        size_t index)
{
    char key[16];

    make_entry_key(
        index,
        key,
        sizeof(key));


    esp_err_t err =
        nvs_erase_key(
            s_nvs_handle,
            key);


    if(err == ESP_ERR_NVS_NOT_FOUND)
    {
        return true;
    }


    if(err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to erase sensor %u: %s",
            (unsigned)index,
            esp_err_to_name(err));

        return false;
    }


    return true;
}


/* -------------------------------------------------------------------------- */
/* Initialization                                                             */
/* -------------------------------------------------------------------------- */

bool xiaomi_registry_init(void)
{
    if(s_initialized)
    {
        return true;
    }


    esp_err_t err =
        nvs_open(
            NVS_NAMESPACE,
            NVS_READWRITE,
            &s_nvs_handle);


    if(err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "nvs_open failed: %s",
            esp_err_to_name(err));

        return false;
    }


    size_t count = 0;


    if(!get_count(
            &count))
    {
        nvs_close(
            s_nvs_handle);

        s_nvs_handle = 0;

        return false;
    }


    s_initialized = true;


    ESP_LOGI(
        TAG,
        "Sensor registry ready: %u sensors",
        (unsigned)count);


    return true;
}


/* -------------------------------------------------------------------------- */
/* Find                                                                      */
/* -------------------------------------------------------------------------- */

bool xiaomi_registry_find_key(
        const uint8_t mac[6],
        uint8_t key[XIAOMI_BIND_KEY_SIZE])
{
    if(!s_initialized ||
       mac == NULL ||
       key == NULL)
    {
        return false;
    }


    size_t count = 0;

    if(!get_count(
            &count))
    {
        return false;
    }


    for(size_t i = 0;
        i < count;
        i++)
    {
        xiaomi_registry_entry_t entry;


        if(!load_entry(
                i,
                &entry))
        {
            continue;
        }


        if(memcmp(
                entry.mac,
                mac,
                6) == 0)
        {
            memcpy(
                key,
                entry.key,
                XIAOMI_BIND_KEY_SIZE);

            return true;
        }
    }


    return false;
}


/* -------------------------------------------------------------------------- */
/* Add                                                                       */
/* -------------------------------------------------------------------------- */

bool xiaomi_registry_add(
        const uint8_t mac[6],
        const uint8_t key[XIAOMI_BIND_KEY_SIZE])
{
    if(!s_initialized ||
       mac == NULL ||
       key == NULL)
    {
        return false;
    }


    size_t count = 0;

    if(!get_count(
            &count))
    {
        return false;
    }


    /*
     * First check whether this MAC already exists.
     *
     * If it does, replace only the key.
     */
    for(size_t i = 0;
        i < count;
        i++)
    {
        xiaomi_registry_entry_t entry;


        if(!load_entry(
                i,
                &entry))
        {
            continue;
        }


        if(memcmp(
                entry.mac,
                mac,
                6) == 0)
        {
            memcpy(
                entry.key,
                key,
                XIAOMI_BIND_KEY_SIZE);


            if(!save_entry(
                    i,
                    &entry))
            {
                return false;
            }


            if(nvs_commit(
                    s_nvs_handle) != ESP_OK)
            {
                ESP_LOGE(
                    TAG,
                    "NVS commit failed");

                return false;
            }


            ESP_LOGI(
                TAG,
                "Sensor key updated");


            return true;
        }
    }


    /*
     * New sensor.
     */
    if(count >= XIAOMI_REGISTRY_MAX_DEVICES)
    {
        ESP_LOGW(
            TAG,
            "Sensor registry is full");

        return false;
    }


    xiaomi_registry_entry_t entry;


    memcpy(
        entry.mac,
        mac,
        6);


    memcpy(
        entry.key,
        key,
        XIAOMI_BIND_KEY_SIZE);


    if(!save_entry(
            count,
            &entry))
    {
        return false;
    }


    count++;


    if(!set_count(
            count))
    {
        return false;
    }


    if(nvs_commit(
            s_nvs_handle) != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "NVS commit failed");

        return false;
    }


    ESP_LOGI(
        TAG,
        "Sensor added");


    return true;
}


/* -------------------------------------------------------------------------- */
/* Remove                                                                     */
/* -------------------------------------------------------------------------- */

bool xiaomi_registry_remove(
        const uint8_t mac[6])
{
    if(!s_initialized ||
       mac == NULL)
    {
        return false;
    }


    size_t count = 0;

    if(!get_count(
            &count))
    {
        return false;
    }


    size_t found = count;


    for(size_t i = 0;
        i < count;
        i++)
    {
        xiaomi_registry_entry_t entry;


        if(!load_entry(
                i,
                &entry))
        {
            continue;
        }


        if(memcmp(
                entry.mac,
                mac,
                6) == 0)
        {
            found = i;
            break;
        }
    }


    if(found == count)
    {
        return false;
    }


    /*
     * Keep entries compact.
     *
     * If the removed entry is not the last one,
     * move the last entry into its place.
     */
    if(found != count - 1)
    {
        xiaomi_registry_entry_t last;


        if(!load_entry(
                count - 1,
                &last))
        {
            return false;
        }


        if(!save_entry(
                found,
                &last))
        {
            return false;
        }
    }


    /*
     * Erase the old last entry.
     */
    if(!erase_entry(
            count - 1))
    {
        return false;
    }


    count--;


    if(!set_count(
            count))
    {
        return false;
    }


    if(nvs_commit(
            s_nvs_handle) != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "NVS commit failed");

        return false;
    }


    ESP_LOGI(
        TAG,
        "Sensor removed");


    return true;
}


/* -------------------------------------------------------------------------- */
/* Enumeration                                                                */
/* -------------------------------------------------------------------------- */

size_t xiaomi_registry_count(void)
{
    if(!s_initialized)
    {
        return 0;
    }


    size_t count = 0;


    if(!get_count(
            &count))
    {
        return 0;
    }


    return count;
}


bool xiaomi_registry_get(
        size_t index,
        xiaomi_registry_entry_t *entry)
{
    if(!s_initialized ||
       entry == NULL)
    {
        return false;
    }


    size_t count = 0;


    if(!get_count(
            &count))
    {
        return false;
    }


    if(index >= count)
    {
        return false;
    }


    return load_entry(
        index,
        entry);
}