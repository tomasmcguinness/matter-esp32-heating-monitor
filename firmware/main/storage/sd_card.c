#include "sd_card.h"

#include <inttypes.h>
#include <stdlib.h>

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

static sd_card_state_t s_state = SD_CARD_ABSENT;
static esp_err_t       s_last_error = ESP_OK;
static sdmmc_csd_t     s_csd;          // Valid whenever s_state != SD_CARD_ABSENT.
static bool            s_csd_valid = false;

bool sd_card_available(void)
{
    return s_available;
}

sd_card_state_t sd_card_get_state(void)
{
    return s_state;
}

esp_err_t sd_card_last_error(void)
{
    return s_last_error;
}

uint64_t sd_card_get_capacity_bytes(void)
{
    if (!s_csd_valid) {
        return 0;
    }
    return (uint64_t)s_csd.capacity * (uint64_t)s_csd.sector_size;
}

// esp_vfs_fat_sdspi_mount() frees its own sdmmc_card_t on every failure path and only
// ever assigns *out_card after the mount has succeeded, so a failed mount leaves us with
// no CSD to report. Re-attach and probe once, purely to find out whether a card is
// actually there. Cheap, and only ever runs on a path that previously just gave up.
//
// Safe to do here because the mount's cleanup calls sdspi_host_remove_device() but never
// sdspi_host_deinit(), and the SPI bus is ours -- spi_bus_free() has not run yet.
static void probe_for_card(const sdspi_device_config_t *slot)
{
    sdspi_dev_handle_t handle;

    if (sdspi_host_init_device(slot, &handle) != ESP_OK) {
        return;
    }

    sdmmc_host_t probe_host = SDSPI_HOST_DEFAULT();
    probe_host.slot = handle;

    sdmmc_card_t *probe_card = calloc(1, sizeof(sdmmc_card_t));

    if (probe_card != NULL && sdmmc_card_init(&probe_host, probe_card) == ESP_OK) {
        s_csd = probe_card->csd;
        s_csd_valid = true;
    }

    free(probe_card);

    // Unconditional, and nothing above may return early: spi_bus_free() refuses while a
    // device is still attached, so bailing out here would turn a missing card into a
    // leaked SPI device and DMA buffer.
    sdspi_host_remove_device(handle);
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
        s_last_error = ret;
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
        s_last_error = ret;

        // ESP_FAIL means the mount itself failed, which it can only reach once
        // sdmmc_card_init() has already succeeded -- so a card is definitely present,
        // whatever the probe below then makes of it. Any other code means the card never
        // came up at all.
        s_state = (ret == ESP_FAIL) ? SD_CARD_NO_FILESYSTEM : SD_CARD_ABSENT;

        probe_for_card(&slot);

        // A card that answers the probe is present even if the mount reported something
        // other than ESP_FAIL.
        if (s_csd_valid) {
            s_state = SD_CARD_NO_FILESYSTEM;
            ESP_LOGW(TAG, "SD card present (%" PRIu64 " bytes) but carries no FAT volume",
                     sd_card_get_capacity_bytes());
        }

        spi_bus_free(SD_SPI_HOST);
        return ret;
    }

    s_available = true;
    s_state = SD_CARD_MOUNTED;
    s_last_error = ESP_OK;
    s_csd = s_card->csd;
    s_csd_valid = true;
    ESP_LOGI(TAG, "SD card mounted at %s", SD_CARD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}
