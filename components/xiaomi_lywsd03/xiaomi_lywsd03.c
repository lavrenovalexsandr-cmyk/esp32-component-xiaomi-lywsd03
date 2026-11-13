#include "xiaomi_lywsd03.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_log.h"
#include "psa/crypto.h"

#include "xiaomi_registry.h"


static const char *TAG = "XIAOMI";


#define XIAOMI_LYWSD03_PRODUCT_ID 0x055B
#define MI_FC_ENCRYPTED 0x08


static const uint8_t *find_mibeacon_data(
        const platform_ble_adv_packet_t *packet)
{
    if(packet == NULL ||
       packet->data == NULL ||
       packet->data_len < 4)
    {
        return NULL;
    }

    for(size_t i = 0;
        i + 3 < packet->data_len;
        i++)
    {
        if(packet->data[i] == 0x95 &&
           packet->data[i + 1] == 0xFE)
        {
            return &packet->data[i + 2];
        }
    }

    return NULL;
}


static void log_mac(
        const uint8_t *mac)
{
    ESP_LOGI(
        TAG,
        "MAC %02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
}


/*
 * Извлекает MAC из MiBeacon.
 *
 * MiBeacon хранит MAC в обратном порядке.
 */
bool xiaomi_lywsd03_get_info(
        const platform_ble_adv_packet_t *packet,
        uint8_t mac[6])
{
    if(packet == NULL ||
       packet->data == NULL ||
       mac == NULL)
    {
        return false;
    }

    const uint8_t *mi_data =
        find_mibeacon_data(packet);

    if(mi_data == NULL)
    {
        return false;
    }

    size_t offset =
        (size_t)(mi_data - packet->data);

    size_t len =
        packet->data_len - offset;

    if(len < 11)
    {
        return false;
    }

    uint16_t product_id =
        (uint16_t)mi_data[2] |
        ((uint16_t)mi_data[3] << 8);

    if(product_id !=
       XIAOMI_LYWSD03_PRODUCT_ID)
    {
        return false;
    }

    /*
     * MiBeacon stores the MAC in reverse order.
     */
    mac[0] = mi_data[10];
    mac[1] = mi_data[9];
    mac[2] = mi_data[8];
    mac[3] = mi_data[7];
    mac[4] = mi_data[6];
    mac[5] = mi_data[5];

    return true;
}


bool xiaomi_lywsd03_is_packet(
        const platform_ble_adv_packet_t *packet)
{
    uint8_t mac[6];

    return xiaomi_lywsd03_get_info(
        packet,
        mac);
}


bool xiaomi_lywsd03_has_key(
        const uint8_t mac[6])
{
    if(mac == NULL)
    {
        return false;
    }

    uint8_t key[XIAOMI_BIND_KEY_SIZE];

    return xiaomi_registry_find_key(
        mac,
        key);
}


/* -------------------------------------------------------------------------- */
/* AES-CCM                                                                    */
/* -------------------------------------------------------------------------- */

static bool decrypt_mibeacon(
        const uint8_t *mi_data,
        size_t mi_len,
        const uint8_t *key,
        uint8_t *plaintext,
        size_t plaintext_size,
        size_t *plaintext_len)
{
    if(mi_data == NULL ||
       key == NULL ||
       plaintext == NULL ||
       plaintext_len == NULL ||
       mi_len < 19)
    {
        return false;
    }

    uint8_t frame_counter =
        mi_data[4];

    const uint8_t *mac =
        &mi_data[5];

    size_t ext_counter_offset =
        mi_len - 7;

    size_t mic_offset =
        mi_len - 4;

    size_t ciphertext_offset =
        11;

    if(ext_counter_offset <= ciphertext_offset)
    {
        ESP_LOGE(
            TAG,
            "Invalid MiBeacon encrypted frame length: %u",
            (unsigned)mi_len);

        return false;
    }

    size_t ciphertext_len =
        ext_counter_offset - ciphertext_offset;

    if(ciphertext_len == 0)
    {
        ESP_LOGE(
            TAG,
            "Empty encrypted payload");

        return false;
    }

    if(ciphertext_len > plaintext_size)
    {
        ESP_LOGE(
            TAG,
            "Plaintext buffer too small: need=%u have=%u",
            (unsigned)ciphertext_len,
            (unsigned)plaintext_size);

        return false;
    }

    uint8_t nonce[12];

    memcpy(
        &nonce[0],
        mac,
        6);

    nonce[6] =
        mi_data[2];

    nonce[7] =
        mi_data[3];

    nonce[8] =
        frame_counter;

    memcpy(
        &nonce[9],
        &mi_data[ext_counter_offset],
        3);

    uint8_t cipher_and_tag[64];

    if(ciphertext_len + 4 >
       sizeof(cipher_and_tag))
    {
        ESP_LOGE(
            TAG,
            "Encrypted payload too large");

        return false;
    }

    memcpy(
        cipher_and_tag,
        &mi_data[ciphertext_offset],
        ciphertext_len);

    memcpy(
        cipher_and_tag + ciphertext_len,
        &mi_data[mic_offset],
        4);

    psa_key_attributes_t attributes =
        PSA_KEY_ATTRIBUTES_INIT;

    psa_set_key_type(
        &attributes,
        PSA_KEY_TYPE_AES);

    psa_set_key_bits(
        &attributes,
        128);

    psa_set_key_usage_flags(
        &attributes,
        PSA_KEY_USAGE_DECRYPT);

    psa_set_key_algorithm(
        &attributes,
        PSA_ALG_AEAD_WITH_SHORTENED_TAG(
            PSA_ALG_CCM,
            4));

    psa_key_id_t key_id =
        PSA_KEY_ID_NULL;

    psa_status_t status =
        psa_import_key(
            &attributes,
            key,
            16,
            &key_id);

    psa_reset_key_attributes(
        &attributes);

    if(status != PSA_SUCCESS)
    {
        ESP_LOGE(
            TAG,
            "psa_import_key failed: %d",
            (int)status);

        return false;
    }

    static const uint8_t aad[] =
    {
        0x11
    };

    size_t output_len = 0;

    status =
        psa_aead_decrypt(
            key_id,
            PSA_ALG_AEAD_WITH_SHORTENED_TAG(
                PSA_ALG_CCM,
                4),
            nonce,
            sizeof(nonce),
            aad,
            sizeof(aad),
            cipher_and_tag,
            ciphertext_len + 4,
            plaintext,
            plaintext_size,
            &output_len);

    psa_destroy_key(
        key_id);

    if(status != PSA_SUCCESS)
    {
        ESP_LOGE(
            TAG,
            "AES-CCM decrypt failed: %d",
            (int)status);

        return false;
    }

    *plaintext_len =
        output_len;

    return true;
}


/* -------------------------------------------------------------------------- */
/* MiBeacon objects                                                           */
/* -------------------------------------------------------------------------- */

static void parse_plaintext(
        const uint8_t *data,
        size_t len,
        xiaomi_lywsd03_data_t *sensor)
{
    size_t pos = 0;

    while(pos + 3 <= len)
    {
        uint16_t type =
            (uint16_t)data[pos] |
            ((uint16_t)data[pos + 1] << 8);

        uint8_t object_len =
            data[pos + 2];

        pos += 3;

        if(pos + object_len > len)
        {
            ESP_LOGW(
                TAG,
                "Invalid MiBeacon object: type=0x%04X len=%u",
                type,
                object_len);

            break;
        }

        switch(type)
        {
            case 0x1004:
            {
                if(object_len >= 2)
                {
                    int16_t temp_raw =
                        (int16_t)(
                            (uint16_t)data[pos] |
                            ((uint16_t)data[pos + 1] << 8));

                    sensor->temperature =
                        temp_raw / 10.0f;

                    sensor->temperature_valid =
                        true;
                }

                break;
            }


            case 0x1006:
            {
                if(object_len >= 2)
                {
                    uint16_t hum_raw =
                        (uint16_t)data[pos] |
                        ((uint16_t)data[pos + 1] << 8);

                    sensor->humidity =
                        hum_raw / 10.0f;

                    sensor->humidity_valid =
                        true;
                }

                break;
            }


            case 0x100A:
            {
                if(object_len >= 1)
                {
                    sensor->battery =
                        data[pos];

                    sensor->battery_valid =
                        true;
                }

                break;
            }


            case 0x100D:
            {
                if(object_len >= 4)
                {
                    int16_t temp_raw =
                        (int16_t)(
                            (uint16_t)data[pos] |
                            ((uint16_t)data[pos + 1] << 8));

                    uint16_t hum_raw =
                        (uint16_t)data[pos + 2] |
                        ((uint16_t)data[pos + 3] << 8);

                    sensor->temperature =
                        temp_raw / 10.0f;

                    sensor->humidity =
                        hum_raw / 10.0f;

                    sensor->temperature_valid =
                        true;

                    sensor->humidity_valid =
                        true;
                }

                break;
            }


            default:
            {
                break;
            }
        }

        pos += object_len;
    }
}


/* -------------------------------------------------------------------------- */
/* Main parser                                                                */
/* -------------------------------------------------------------------------- */

static bool xiaomi_lywsd03_parse(
        const platform_ble_adv_packet_t *packet,
        xiaomi_lywsd03_data_t *data)
{
    if(packet == NULL ||
       packet->data == NULL ||
       data == NULL)
    {
        return false;
    }

    memset(
        data,
        0,
        sizeof(*data));

    const uint8_t *mi_data =
        find_mibeacon_data(packet);

    if(mi_data == NULL)
    {
        return false;
    }

    size_t mi_offset =
        (size_t)(mi_data - packet->data);

    size_t mi_len =
        packet->data_len - mi_offset;

    if(mi_len < 11)
    {
        return false;
    }

    uint16_t frame_control =
        (uint16_t)mi_data[0] |
        ((uint16_t)mi_data[1] << 8);

    uint16_t product_id =
        (uint16_t)mi_data[2] |
        ((uint16_t)mi_data[3] << 8);

    if(product_id !=
       XIAOMI_LYWSD03_PRODUCT_ID)
    {
        return false;
    }

    /*
     * MiBeacon stores the MAC in reverse order.
     */
    uint8_t mac[6];

    mac[0] = mi_data[10];
    mac[1] = mi_data[9];
    mac[2] = mi_data[8];
    mac[3] = mi_data[7];
    mac[4] = mi_data[6];
    mac[5] = mi_data[5];

    memcpy(
        data->mac,
        mac,
        sizeof(mac));

    log_mac(mac);


    /*
     * Ключ теперь берём из runtime/NVS registry.
     *
     * Если датчик ещё не добавлен —
     * расшифровка вообще не выполняется.
     */
    uint8_t key[XIAOMI_BIND_KEY_SIZE];

    if(!xiaomi_registry_find_key(
            mac,
            key))
    {
        ESP_LOGW(
            TAG,
            "No BLE key configured for this sensor");

        return false;
    }


    bool encrypted =
        (frame_control & MI_FC_ENCRYPTED) != 0;

    if(!encrypted)
    {
        ESP_LOGW(
            TAG,
            "MiBeacon packet is not encrypted, FC=0x%04X",
            frame_control);

        return false;
    }


    if(mi_len < 19)
    {
        ESP_LOGE(
            TAG,
            "Encrypted MiBeacon packet too short: %u",
            (unsigned)mi_len);

        return false;
    }


    uint8_t plaintext[64];

    size_t plaintext_len = 0;


    if(!decrypt_mibeacon(
            mi_data,
            mi_len,
            key,
            plaintext,
            sizeof(plaintext),
            &plaintext_len))
    {
        return false;
    }


    parse_plaintext(
        plaintext,
        plaintext_len,
        data);


    /*
     * The packet is considered successfully decoded only
     * when it contains at least one known measurement.
     */
    if(!data->temperature_valid &&
       !data->humidity_valid &&
       !data->battery_valid)
    {
        ESP_LOGW(
            TAG,
            "No supported sensor data in MiBeacon packet");

        return false;
    }


    if(data->temperature_valid)
    {
        ESP_LOGI(
            TAG,
            "Temperature %.1f C",
            data->temperature);
    }


    if(data->humidity_valid)
    {
        ESP_LOGI(
            TAG,
            "Humidity %.1f %%",
            data->humidity);
    }


    if(data->battery_valid)
    {
        ESP_LOGI(
            TAG,
            "Battery %u %%",
            data->battery);
    }


    return true;
}


static xiaomi_lywsd03_data_callback_t s_data_callback;
static void *s_data_callback_context;

esp_err_t xiaomi_lywsd03_init(void)
{
    if (!xiaomi_registry_init()) {
        return ESP_FAIL;
    }

    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %d", (int)status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void xiaomi_lywsd03_set_callback(
    xiaomi_lywsd03_data_callback_t callback,
    void *context)
{
    s_data_callback = callback;
    s_data_callback_context = context;
}

bool xiaomi_lywsd03_process_adv(
    const platform_ble_adv_packet_t *packet)
{
    xiaomi_lywsd03_data_t data = {0};

    if (!xiaomi_lywsd03_parse(packet, &data)) {
        return false;
    }

    if (s_data_callback != NULL) {
        s_data_callback(&data, s_data_callback_context);
    }

    return true;
}

bool xiaomi_lywsd03_add_sensor(
    const uint8_t mac[6],
    const uint8_t key[XIAOMI_BIND_KEY_SIZE])
{
    return xiaomi_registry_add(mac, key);
}

bool xiaomi_lywsd03_remove_sensor(const uint8_t mac[6])
{
    return xiaomi_registry_remove(mac);
}

size_t xiaomi_lywsd03_sensor_count(void)
{
    return xiaomi_registry_count();
}
