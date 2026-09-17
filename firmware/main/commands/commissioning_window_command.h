#pragma once

#include <esp_err.h>
#include <lib/core/CHIPError.h>

#include <cstdint>

namespace heating_monitor
{
    namespace controller
    {
        // How long the device stays open for commissioning. 900 s is the spec maximum for an
        // enhanced window, which leaves time to find the device in another ecosystem's app.
        constexpr uint16_t kCommissioningWindowTimeoutSeconds = 900;

        typedef struct
        {
            char manual_code[24]; // 11 digits (or 21 with VID/PID), NUL-terminated
            char qr_code[64];     // "MT:..."
            uint16_t timeout_s;
        } commissioning_window_result_t;

        // Opens an enhanced commissioning window on an already-commissioned node, with a random
        // passcode and discriminator, and blocks until the device answers or the wait times out.
        //
        // Must be called WITHOUT the CHIP stack lock held -- it takes the lock itself and then
        // waits on a callback that runs on the Matter task.
        //
        // Returns ESP_OK and fills `out` on success. ESP_ERR_INVALID_STATE means another request
        // is still running, ESP_ERR_TIMEOUT means the device never answered, and ESP_FAIL means
        // the device or the session refused, with the CHIP error in `chip_err`.
        esp_err_t open_commissioning_window(uint64_t node_id, commissioning_window_result_t *out, CHIP_ERROR *chip_err);

    } // namespace controller
} // namespace heating_monitor
