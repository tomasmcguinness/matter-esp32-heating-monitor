#include "mqtt_publisher.h"

#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_publisher";

// One slot per topic. The home, a state and a discovery topic per room and radiator, and room to
// spare for a house with 8 rooms and 12 radiators or a good deal more.
#define SLOT_COUNT 96

// Long enough to catch the rest of a burst, short enough that nobody notices the delay.
#define COALESCE_MS 100

// Holds cJSON building the home payload, on top of the publish itself.
#define TASK_STACK 6144
#define TASK_PRIORITY 3

typedef struct
{
    char *topic; // NULL when the slot is free
    char *payload;
    esp_mqtt_client_handle_t client;
    bool retain;
    uint32_t order;
} slot_t;

static slot_t *s_slots = NULL;
static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static uint32_t s_next_order = 0;
static uint32_t s_dropped = 0;

static std::atomic<void (*)(void *)> s_deferred_fn{nullptr};
static std::atomic<void *> s_deferred_arg{nullptr};

// In PSRAM where it can be: the home payload is a couple of kilobytes and this table can hold
// about a hundred of these at once.
static char *copy_string(const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = (char *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (copy == NULL)
    {
        copy = (char *)malloc(size);
    }

    if (copy != NULL)
    {
        memcpy(copy, value, size);
    }

    return copy;
}

// The oldest queued slot, moved out of the table. Returns false when there is none.
static bool take_oldest(slot_t *out)
{
    bool found = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    slot_t *oldest = NULL;

    for (size_t i = 0; i < SLOT_COUNT; i++)
    {
        slot_t *slot = &s_slots[i];

        // Compared by difference so that the counter can wrap.
        if (slot->topic != NULL && (oldest == NULL || (int32_t)(slot->order - oldest->order) < 0))
        {
            oldest = slot;
        }
    }

    if (oldest != NULL)
    {
        *out = *oldest;
        oldest->topic = NULL;
        oldest->payload = NULL;
        found = true;
    }

    xSemaphoreGive(s_lock);

    return found;
}

static void publisher_task(void *)
{
    uint32_t reported_dropped = 0;

    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(COALESCE_MS));

        void (*fn)(void *) = s_deferred_fn.exchange(nullptr);

        if (fn != nullptr)
        {
            fn(s_deferred_arg.load());
        }

        slot_t slot;

        while (take_oldest(&slot))
        {
            // Retained messages are the discovery configs, which go missing silently otherwise. A
            // failed state publish is an ordinary disconnect, and the next reading replaces it.
            int msg_id = esp_mqtt_client_publish(slot.client, slot.topic, slot.payload, 0, 0, slot.retain ? 1 : 0);

            if (msg_id < 0 && slot.retain)
            {
                ESP_LOGE(TAG, "Failed to publish retained %s (%u bytes); is the MQTT out buffer large enough?",
                         slot.topic, (unsigned)strlen(slot.payload));
            }

            free(slot.topic);
            free(slot.payload);
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint32_t dropped = s_dropped;
        xSemaphoreGive(s_lock);

        if (dropped != reported_dropped)
        {
            ESP_LOGW(TAG, "%lu publishes dropped so far, with every slot in use", (unsigned long)dropped);
            reported_dropped = dropped;
        }
    }
}

void mqtt_publisher_start(void)
{
    if (s_task != NULL)
    {
        return;
    }

    s_slots = (slot_t *)heap_caps_calloc(SLOT_COUNT, sizeof(slot_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_lock = xSemaphoreCreateMutex();

    if (s_slots == NULL || s_lock == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate the publish table");
        return;
    }

    if (xTaskCreate(publisher_task, "mqtt_pub", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create the publisher task");
        s_task = NULL;
    }
}

bool mqtt_publisher_publish(esp_mqtt_client_handle_t client, const char *topic, const char *payload, bool retain)
{
    if (s_task == NULL || client == NULL || topic == NULL || payload == NULL)
    {
        return false;
    }

    // Allocated outside the lock, which is only ever held to move pointers around.
    char *payload_copy = copy_string(payload);
    char *topic_copy = copy_string(topic);

    if (payload_copy == NULL || topic_copy == NULL)
    {
        free(payload_copy);
        free(topic_copy);
        return false;
    }

    char *replaced_payload = NULL;
    bool queued = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    slot_t *free_slot = NULL;

    for (size_t i = 0; i < SLOT_COUNT; i++)
    {
        slot_t *slot = &s_slots[i];

        if (slot->topic == NULL)
        {
            free_slot = free_slot == NULL ? slot : free_slot;
        }
        else if (strcmp(slot->topic, topic) == 0)
        {
            replaced_payload = slot->payload;
            slot->payload = payload_copy;
            slot->retain = retain;
            slot->client = client;
            queued = true;
            break;
        }
    }

    bool topic_kept = false;

    if (!queued && free_slot != NULL)
    {
        free_slot->topic = topic_copy;
        free_slot->payload = payload_copy;
        free_slot->retain = retain;
        free_slot->client = client;
        free_slot->order = s_next_order++;
        topic_kept = true;
        queued = true;
    }
    else if (!queued)
    {
        s_dropped++;
    }

    xSemaphoreGive(s_lock);

    if (!topic_kept)
    {
        free(topic_copy);
    }

    free(replaced_payload);

    if (!queued)
    {
        free(payload_copy);
        return false;
    }

    xTaskNotifyGive(s_task);

    return true;
}

void mqtt_publisher_defer(void (*fn)(void *), void *arg)
{
    if (s_task == NULL)
    {
        return;
    }

    s_deferred_arg.store(arg);
    s_deferred_fn.store(fn);

    xTaskNotifyGive(s_task);
}
