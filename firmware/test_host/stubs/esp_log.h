#pragma once
// Host stand-in for ESP-IDF's esp_log.h: logging is dropped.
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
