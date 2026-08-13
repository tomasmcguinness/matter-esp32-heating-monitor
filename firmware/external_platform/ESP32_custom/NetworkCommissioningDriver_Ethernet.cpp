/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/*
 * Heating monitor: the upstream implementation of ESPEthernetDriver::Init() targets the
 * ESP32 internal EMAC (esp_eth_mac_new_esp32 / eth_esp32_emac_config_t), which does not
 * exist on the ESP32-S3. This board (Waveshare ESP32-S3-ETH) carries a W5500 SPI Ethernet
 * controller instead, so Init() is replaced with a W5500 bring-up.
 *
 * See espressif/esp-matter#1785.
 */

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/mld6.h"
#include "lwip/netif.h"
#include <platform/ESP32_custom/NetworkCommissioningDriver.h>
#include <platform/ESP32_custom/route_hook/ESP32RouteHook.h>

// Waveshare ESP32-S3-ETH W5500 wiring.
#define ETH_SPI_HOST SPI2_HOST
#define ETH_SPI_SCLK_GPIO 13
#define ETH_SPI_MOSI_GPIO 11
#define ETH_SPI_MISO_GPIO 12
#define ETH_SPI_CS_GPIO 14
#define ETH_SPI_INT_GPIO 10
#define ETH_SPI_RST_GPIO 9
#define ETH_SPI_CLOCK_MHZ 25

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

// Tracks whether we hold a membership on ff02::1, so a link bounce re-joins exactly once
// rather than stacking up lwIP group use counts.
static bool sJoinedAllNodes = false;

// Join the all-nodes multicast group (ff02::1). lwIP never joins it of its own accord, so
// no MLD report is ever sent for it and an MLD-snooping switch prunes it on our port. That
// costs us the border router's RAs directly, and it also breaks everything else: MLD general
// queries are themselves addressed to ff02::1, so without the membership we are never
// queried, never refresh any other group, and the switch eventually ages out mDNS (ff02::fb)
// as well — at which point commissionable-node discovery browses and hears nothing back.
//
// mld6_joingroup_netif() touches the group list, so it has to run in the TCPIP context.
static esp_err_t join_all_nodes_cb(void * ctx)
{
    struct netif * lwip_netif = (struct netif *) ctx;
    ip6_addr_t allnodes;
    IP6_ADDR(&allnodes, PP_HTONL(0xff020000), PP_HTONL(0x00000000), PP_HTONL(0x00000000), PP_HTONL(0x00000001));
    ip6_addr_assign_zone(&allnodes, IP6_MULTICAST, lwip_netif);
    return (mld6_joingroup_netif(lwip_netif, &allnodes) == ERR_OK) ? ESP_OK : ESP_FAIL;
}

static void on_ip6_event(void * esp_netif, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    ip_event_got_ip6_t * event = (ip_event_got_ip6_t *) event_data;
    // GOT_IP6 fires once per address (link-local, then any global one), and for every
    // netif in the system — we only want our own Ethernet interface, once.
    VerifyOrReturn(event != nullptr && event->esp_netif == (esp_netif_t *) esp_netif);
    VerifyOrReturn(!sJoinedAllNodes);

    struct netif * lwip_netif = (struct netif *) esp_netif_get_netif_impl(event->esp_netif);
    VerifyOrReturn(lwip_netif != nullptr);

    esp_err_t err = esp_netif_tcpip_exec(join_all_nodes_cb, lwip_netif);
    sJoinedAllNodes = (err == ESP_OK);
    ChipLogProgress(DeviceLayer, "MLD join ff02::1: %s", esp_err_to_name(err));
}

static void on_eth_event(void * esp_netif, esp_event_base_t event_base, int32_t event_id, void * event_data)
{
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED: {
        esp_netif_t * eth_netif = (esp_netif_t *) esp_netif;
        ChipLogProgress(DeviceLayer, "Ethernet Connected");
        // Mark the interface as MLD6-capable before any address is configured. lwIP gates
        // nd6_adjust_mld_membership() on this flag (netif.c), so without it we never join
        // the solicited-node group for our own addresses and neighbour solicitations aimed
        // at us are pruned by an MLD-snooping switch. esp_netif does not set it for Ethernet.
        struct netif * lwip_netif = (struct netif *) esp_netif_get_netif_impl(eth_netif);
        if (lwip_netif != nullptr)
        {
            netif_set_flags(lwip_netif, NETIF_FLAG_MLD6);
        }
        ESP_ERROR_CHECK(esp_netif_create_ip6_linklocal(eth_netif));
        // Install the lwIP route hook so that route information options in the border
        // router's RAs are honoured. On Wi-Fi builds ConnectivityManagerImpl_WiFi does
        // this for us; on Ethernet nothing else will, and without it the controller has
        // no route to the Thread sensors' off-mesh addresses.
        esp_route_hook_init(eth_netif);
    }
    break;
    case ETHERNET_EVENT_DISCONNECTED:
        ChipLogProgress(DeviceLayer, "Ethernet Disconnected");
        // The netif tears its group memberships down with the link, so re-join on the way
        // back up rather than assuming the old membership survived.
        sJoinedAllNodes = false;
        break;
    default:
        break;
    }
}

CHIP_ERROR ESPEthernetDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    // The W5500 signals RX through a level-triggered interrupt, which needs per-pin ISRs.
    esp_err_t isr_err = gpio_install_isr_service(0);
    VerifyOrReturnError(isr_err == ESP_OK || isr_err == ESP_ERR_INVALID_STATE, CHIP_ERROR_INTERNAL);

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t * eth_netif      = esp_netif_new(&netif_cfg);
    VerifyOrReturnError(eth_netif != nullptr, CHIP_ERROR_NO_MEMORY);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num      = ETH_SPI_MOSI_GPIO;
    buscfg.miso_io_num      = ETH_SPI_MISO_GPIO;
    buscfg.sclk_io_num      = ETH_SPI_SCLK_GPIO;
    buscfg.quadwp_io_num    = -1;
    buscfg.quadhd_io_num    = -1;
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t spi_devcfg = {};
    spi_devcfg.mode                          = 0;
    spi_devcfg.clock_speed_hz                = ETH_SPI_CLOCK_MHZ * 1000 * 1000;
    spi_devcfg.spics_io_num                  = ETH_SPI_CS_GPIO;
    spi_devcfg.queue_size                    = 20;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num       = ETH_SPI_INT_GPIO;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t * mac         = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    VerifyOrReturnError(mac != nullptr, CHIP_ERROR_INTERNAL);

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num   = ETH_SPI_RST_GPIO;
    esp_eth_phy_t * phy         = esp_eth_phy_new_w5500(&phy_config);
    VerifyOrReturnError(phy != nullptr, CHIP_ERROR_INTERNAL);

    esp_eth_config_t eth_cfg    = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = nullptr;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &eth_handle));

    // The W5500 has no MAC address of its own, so derive one from the SoC's eFuse.
    uint8_t mac_addr[6] = { 0 };
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));

    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_eth_event, eth_netif));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &on_ip6_event, eth_netif));
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    ChipLogProgress(DeviceLayer, "W5500 Ethernet initialized");

    return CHIP_NO_ERROR;
}

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
