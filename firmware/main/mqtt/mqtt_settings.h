#pragma once

#include "esp_err.h"

// Where the MQTT broker is and how to log in to it, kept in the NVS namespace "mqtt" and edited
// from the Settings page. An empty URL means MQTT is not used.

#define MQTT_URL_MAX      128
#define MQTT_USERNAME_MAX 65
#define MQTT_PASSWORD_MAX 129

typedef struct
{
    char url[MQTT_URL_MAX];
    char username[MQTT_USERNAME_MAX];
    // Goes to the broker and nowhere else: never log it and never put it in a response.
    char password[MQTT_PASSWORD_MAX];
} mqtt_settings_t;

// Everything is empty when nothing has been saved.
void mqtt_settings_load(mqtt_settings_t *settings);

// An empty `password` keeps the one already stored. ESP_ERR_INVALID_ARG unless the URL is empty
// or an mqtt:// one with no credentials in it -- those belong in the fields, where a GET does
// not hand them back.
esp_err_t mqtt_settings_save(const mqtt_settings_t *settings);
