#include <stdint.h>
#include <esp_matter.h>
#include <esp_matter_client.h>

void attribute_data_cb(uint64_t remote_node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data);