#include <stdint.h>
#include <esp_matter.h>
#include <esp_matter_client.h>

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data);
void attribute_data_read_done(uint64_t remote_node_id, const ScopedMemoryBufferWithSize<AttributePathParams> &attr_path, const ScopedMemoryBufferWithSize<EventPathParams> &event_path);

void node_subscription_established_cb(uint64_t remote_node_id, uint32_t subscription_id);
void node_subscription_terminated_cb(uint64_t remote_node_id, uint32_t subscription_id);
void node_subscribe_failed_cb(void *ctx);
