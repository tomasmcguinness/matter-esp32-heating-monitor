#include "mqtt_settings.h"

#include <cstring>

#include "nvs.h"

#define NVS_NAMESPACE "mqtt"

void mqtt_settings_load(mqtt_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));

    nvs_handle_t nvs;

    // Nothing saved yet is the normal first-boot case, and everything stays empty.
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
    {
        return;
    }

    size_t len = sizeof(settings->url);
    nvs_get_str(nvs, "url", settings->url, &len);

    len = sizeof(settings->username);
    nvs_get_str(nvs, "username", settings->username, &len);

    len = sizeof(settings->password);
    nvs_get_str(nvs, "password", settings->password, &len);

    nvs_close(nvs);
}

esp_err_t mqtt_settings_save(const mqtt_settings_t *settings)
{
    if (settings->url[0] != '\0' &&
        (strncmp(settings->url, "mqtt://", 7) != 0 || strchr(settings->url, '@') != NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    mqtt_settings_t updated = *settings;

    if (updated.password[0] == '\0')
    {
        mqtt_settings_t stored;
        mqtt_settings_load(&stored);
        strlcpy(updated.password, stored.password, sizeof(updated.password));
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);

    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_set_str(nvs, "url", updated.url);
    if (err == ESP_OK) err = nvs_set_str(nvs, "username", updated.username);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", updated.password);
    if (err == ESP_OK) err = nvs_commit(nvs);

    nvs_close(nvs);

    return err;
}
