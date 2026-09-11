#include "sd_card.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_card";

// Waveshare ESP32-S3-ETH. The W5500 owns SPI2 at 25 MHz (see
// external_platform/ESP32_custom/NetworkCommissioningDriver_Ethernet.cpp), so the card
// goes on SPI3, which is otherwise unused. Pins are from the board schematic netlist --
// SD_CS reaches GPIO4 through a series resistor.
#define SD_SPI_HOST      SPI3_HOST
#define SD_PIN_CS        4
#define SD_PIN_MISO      5
#define SD_PIN_MOSI      6
#define SD_PIN_CLK       7

static sdmmc_card_t *s_card = NULL;
static bool          s_available = false;

bool sd_card_available(void)
{
    return s_available;
}

esp_err_t sd_card_init(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SD_PIN_MOSI,
        .miso_io_num     = SD_PIN_MISO,
        .sclk_io_num     = SD_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = SD_SPI_HOST;

    // format_if_mount_failed stays false: a card that fails to mount is far more likely
    // to be someone else's card, or a card the firmware cannot read yet, than one that
    // wants erasing.
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 10,
        .allocation_unit_size   = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(SD_CARD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        // Not fatal. Without a card the live dashboard still works; only history is lost.
        ESP_LOGW(TAG, "SD mount failed (%s) -- history logging disabled", esp_err_to_name(ret));
        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    s_available = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_CARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}
