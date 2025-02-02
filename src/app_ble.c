/**
 *  @file app_ble.c
 *  @author Otso Jousimaa <otso@ojousima.net>
 *  @date 2020-05-12
 *  @copyright Ruuvi Innovations Ltd, license BSD-3-Clause.
 *
 *  Application BLE control, selecting PHYs and channels to scan on.
 */

#include "app_ble.h"
#include <string.h>
#include "app_uart.h"
#include "ruuvi_driver_error.h"
#include "ruuvi_boards.h"
#include "ruuvi_interface_log.h"
#include "ruuvi_interface_communication.h"
#include "ruuvi_interface_communication_radio.h"
#include "ruuvi_interface_communication_ble_advertising.h"
#include "ruuvi_interface_gpio.h"
#include "ruuvi_interface_scheduler.h"
#include "ruuvi_interface_watchdog.h"
#include "ruuvi_task_advertisement.h"
#include "ruuvi_task_led.h"
#if !defined(CEEDLING) && !defined(SONAR)
#include "nrf_log.h"
#else
#define NRF_LOG_INFO(fmt, ...)
#define NRF_LOG_ERROR(fmt, ...)
#endif

#define RB_BLE_UNKNOWN_MANUFACTURER_ID  0xFFFF                  //!< Unknown id
#define RB_BLE_DEFAULT_CH37_STATE       0                       //!< Default channel 37 state
#define RB_BLE_DEFAULT_CH38_STATE       0                       //!< Default channel 38 state
#define RB_BLE_DEFAULT_CH39_STATE       0                       //!< Default channel 39 state
#define RB_BLE_DEFAULT_125KBPS_STATE    false                   //!< Default 125kbps state
#define RB_BLE_DEFAULT_1MBIT_STATE      false                   //!< Default 1mbit state
#define RB_BLE_DEFAULT_2MBIT_STATE      false                   //!< Default 2mbit state
#define RB_BLE_DEFAULT_FLTR_STATE       true                    //!< Default filter id state
#define RB_BLE_DEFAULT_MANUFACTURER_ID  RB_BLE_MANUFACTURER_ID  //!< Default id

static inline void LOG (const char * const msg)
{
    ri_log (RI_LOG_LEVEL_INFO, msg);
}

static inline void LOGD (const char * const msg)
{
    ri_log (RI_LOG_LEVEL_DEBUG, msg);
}

static inline bool scan_is_enabled (const app_ble_scan_t * const params)
{
    return params->modulation_125kbps_enabled || params->modulation_1mbit_enabled
           || params->modulation_2mbit_enabled;
}

static app_ble_scan_t m_scan_params =
{
    .manufacturer_id = RB_BLE_DEFAULT_MANUFACTURER_ID,
    .scan_channels.channel_37 = RB_BLE_DEFAULT_CH37_STATE,
    .scan_channels.channel_38 = RB_BLE_DEFAULT_CH38_STATE,
    .scan_channels.channel_39 = RB_BLE_DEFAULT_CH39_STATE,
    .modulation_125kbps_enabled = RB_BLE_DEFAULT_125KBPS_STATE,
    .modulation_1mbit_enabled = RB_BLE_DEFAULT_1MBIT_STATE,
    .modulation_2mbit_enabled = RB_BLE_DEFAULT_2MBIT_STATE,
    .is_current_modulation_125kbps = false,
    .manufacturer_filter_enabled = RB_BLE_DEFAULT_FLTR_STATE,
};

#ifndef CEEDLING
static
#endif
void repeat_adv (void * p_data, uint16_t data_len)
{
    rd_status_t err_code = RD_SUCCESS;

    if (sizeof (ri_adv_scan_t) == data_len)
    {
        err_code |= app_uart_send_broadcast ((ri_adv_scan_t *) p_data);

        if (RD_SUCCESS == err_code)
        {
            (void) ri_watchdog_feed();
        }
    }
}

/**
 * @brief Handle Scan events.
 *
 * Received data is put to scheduler queue, new scan with new PHY is started on timeout.
 *
 * @param[in] evt Type of event, either RI_COMM_RECEIVED on data or
 *                RI_COMM_TIMEOUT on scan timeout.
 * @param[in] p_data NULL on timeout, ri_adv_scan_t* on received.
 * @param[in] data_len 0 on timeout, size of ri_adv_scan_t on received.
 * @retval RD_SUCCESS on successful handling on event.
 * @retval RD_ERR_NO_MEM if received event could not be put to scheduler queue.
 * @return Error code from scanning if scan cannot be started.
 *
 * @note parameters are not const to maintain compatibility with the event handler
 *       signature.
 **/
#ifndef CEEDLING
static
#endif
rd_status_t on_scan_isr (const ri_comm_evt_t evt, void * p_data, // -V2009
                         size_t data_len)
{
    rd_status_t err_code = RD_SUCCESS;

    switch (evt)
    {
        case RI_COMM_RECEIVED:
            LOGD ("DATA\r\n");
            err_code |= ri_scheduler_event_put (p_data, (uint16_t) data_len, repeat_adv);
            break;

        case RI_COMM_TIMEOUT:
            LOG ("Timeout\r\n");
            err_code |= app_ble_scan_start();
            break;

        default:
            LOG ("Unknown event\r\n");
            break;
    }

    RD_ERROR_CHECK (err_code, ~RD_ERROR_FATAL);
    return err_code;
}

rd_status_t app_ble_manufacturer_filter_set (const bool state)
{
    rd_status_t  err_code = RD_SUCCESS;
    m_scan_params.manufacturer_filter_enabled = state;
    return err_code;
}

bool app_ble_manufacturer_filter_enabled (uint16_t * const p_manufacturer_id)
{
    *p_manufacturer_id = m_scan_params.manufacturer_id;
    return m_scan_params.manufacturer_filter_enabled;
}

rd_status_t app_ble_manufacturer_id_set (const uint16_t id)
{
    rd_status_t  err_code = RD_SUCCESS;
    m_scan_params.manufacturer_id = id;
    return err_code;
}

rd_status_t app_ble_channels_get (ri_radio_channels_t * p_channels)
{
    rd_status_t  err_code = RD_SUCCESS;
    p_channels->channel_37 = m_scan_params.scan_channels.channel_37;
    p_channels->channel_38 = m_scan_params.scan_channels.channel_38;
    p_channels->channel_39 = m_scan_params.scan_channels.channel_39;
    return err_code;
}

rd_status_t app_ble_channels_set (const ri_radio_channels_t channels)
{
    rd_status_t err_code = RD_SUCCESS;

    if ((0 == channels.channel_37)
            && (0 == channels.channel_38)
            && (0 == channels.channel_39))
    {
        err_code |= RD_ERROR_INVALID_PARAM;
    }
    else
    {
        m_scan_params.scan_channels = channels;
    }

    return err_code;
}

void app_ble_set_max_adv_len (uint8_t max_adv_length)
{
    m_scan_params.max_adv_length = max_adv_length;
}

rd_status_t app_ble_modulation_enable (const ri_radio_modulation_t modulation,
                                       const bool enable)
{
    rd_status_t err_code = RD_SUCCESS;

    switch (modulation)
    {
        case RI_RADIO_BLE_125KBPS:
            if (RB_BLE_CODED_SUPPORTED)
            {
                m_scan_params.modulation_125kbps_enabled = enable;
            }
            else
            {
                err_code |= RD_ERROR_NOT_SUPPORTED;
            }

            break;

        case RI_RADIO_BLE_1MBPS:
            m_scan_params.modulation_1mbit_enabled = enable;
            break;

        case RI_RADIO_BLE_2MBPS:
            m_scan_params.modulation_2mbit_enabled = enable;
            break;

        default:
            err_code |= RD_ERROR_INVALID_PARAM;
            break;
    }

    return err_code;
}

static inline void next_modulation_select (void)
{
    if (m_scan_params.is_current_modulation_125kbps)
    {
        if (m_scan_params.modulation_1mbit_enabled ||
                m_scan_params.modulation_2mbit_enabled)
        {
            m_scan_params.is_current_modulation_125kbps = false;
        }
        else
        {
            // No action needed.
        }
    }
    else
    {
        if (m_scan_params.modulation_125kbps_enabled)
        {
            m_scan_params.is_current_modulation_125kbps = true;
        }
        else
        {
            // No action needed.
        }
    }
}

static rd_status_t pa_lna_ctrl (void)
{
    rd_status_t err_code = RD_SUCCESS;
#if RB_PA_ENABLED

    if (!ri_gpio_is_init())
    {
        err_code |= ri_gpio_init();
    }

    // Allow ESP32 to force LNA off for WiFi TX bursts
    err_code |= ri_gpio_configure (RB_PA_CRX_PIN, RI_GPIO_MODE_INPUT_PULLUP);
    err_code |= ri_gpio_configure (RB_PA_CSD_PIN, RI_GPIO_MODE_OUTPUT_STANDARD);
    err_code |= ri_gpio_write (RB_PA_CSD_PIN, RB_PA_CSD_ACTIVE);
#endif
    return err_code;
}

rd_status_t app_ble_scan_start (void)
{
    NRF_LOG_INFO ("app_ble_scan_start");
    rd_status_t err_code = RD_SUCCESS;

    if (scan_is_enabled (&m_scan_params))
    {
        err_code |= rt_adv_uninit();
        err_code |= ri_radio_uninit();
        rt_adv_init_t adv_params =
        {
            .channels = m_scan_params.scan_channels,
            .adv_interval_ms = (1000U), //!< Unused
            .adv_pwr_dbm     = (0),     //!< Unused
            .manufacturer_id = m_scan_params.manufacturer_id,
        };

        if (!m_scan_params.manufacturer_filter_enabled)
        {
            adv_params.manufacturer_id = RB_BLE_UNKNOWN_MANUFACTURER_ID;
        }

        /* When BLE extended advertisement is used, then
         * 1. The primary channel LE 1M PHY (37, 38, 39) is used to notify
         *    the receiver about the subsequent advertisement on the secondary
         *    channel.
         * 2. The receiver switches to the secondary channel 0..36 (LE 2M PHY)
         *
         * So, it is not possible to use only secondary channel 'LE 2M PHY'
         * because we don't know which channel the receiver should listen to.
         *
         * Therefore, we need to enable both primary and secondary channels
         * when extended advertisement is enabled.
         *
         * When Coded PHY (125kbps) is enabled, the data is sent
         * as an extended advertisement only.
         */
        adv_params.is_rx_le_1m_phy_enabled = m_scan_params.modulation_1mbit_enabled;
        adv_params.is_rx_le_2m_phy_enabled = m_scan_params.modulation_2mbit_enabled;
        adv_params.is_rx_le_coded_phy_enabled = m_scan_params.modulation_125kbps_enabled;
        adv_params.max_adv_length = m_scan_params.max_adv_length;

        if (RD_SUCCESS == err_code)
        {
            NRF_LOG_INFO ("PHYs enabled: LE 1M PHY=%d, LE 2M PHY=%d, LE Coded PHY=%d",
                          m_scan_params.modulation_1mbit_enabled,
                          m_scan_params.modulation_2mbit_enabled,
                          m_scan_params.modulation_125kbps_enabled);
            next_modulation_select();
            NRF_LOG_INFO ("Current PHY: %s",
                          m_scan_params.is_current_modulation_125kbps
                          ? "LE Coded PHY"
                          : "LE 1M PHY");
            err_code |= pa_lna_ctrl();
            err_code |= ri_radio_init (m_scan_params.is_current_modulation_125kbps ?
                                       RI_RADIO_BLE_125KBPS : RI_RADIO_BLE_1MBPS);

            if (RD_SUCCESS == err_code)
            {
                err_code |= rt_adv_init (&adv_params);
                err_code |= rt_adv_scan_start (&on_scan_isr);
            }
        }
        else
        {
            NRF_LOG_ERROR ("rt_adv_uninit or ri_radio_uninit failed, err=%d", err_code);
        }
    }
    else
    {
        err_code |= app_ble_scan_stop();
    }

    return err_code;
}

rd_status_t app_ble_scan_stop (void)
{
    rd_status_t err_code = RD_SUCCESS;
    err_code |= rt_adv_scan_stop();
    return err_code;
}
