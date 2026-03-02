#include "aqm_modbus.h"
#include "aqm_modbus_reg.h"
#include "aqm_datastore.h"
#include "aqm_config.h"
#include "mbcontroller.h"

#include <esp_log.h>
#include "driver/gpio.h"

static const char *TAG = "AQM_MODBUS";

#define MODBUS_PORT 502

// Global instances of Modbus registers mapped to memory
holding_reg_params_t holding_reg_params = {0};
input_reg_params_t input_reg_params = {0};

// Handle for the Modbus stack
static void* slave_handle = NULL;

/**
 * @brief Initializes Modbus TCP Slave and maps memory areas
 * * @return esp_err_t ESP_OK on success
 */
esp_err_t aqm_init_modbus(void) {

    mb_communication_info_t comm_info = {0};
    
    /* Configure TCP options for the slave */
    comm_info.tcp_opts.port = MODBUS_PORT;
    comm_info.tcp_opts.mode = MB_TCP;
    comm_info.tcp_opts.addr_type = MB_IPV4;
    comm_info.tcp_opts.ip_addr_table = NULL; /* Bind to any address */
    comm_info.tcp_opts.ip_netif_ptr = NULL;  /* Listen on all network interfaces */
    comm_info.tcp_opts.uid = 1;              /* Modbus Unit ID (Slave ID) */

    esp_err_t err = mbc_slave_create_tcp(&comm_info, &slave_handle);
    if (err != ESP_OK || slave_handle == NULL) {
        ESP_LOGE(TAG, "mbc_slave_create_tcp failed: 0x%x", err);
        return err;
    }

    /* 1. Map HOLDING REGISTERS (Read/Write) */
    mb_register_area_descriptor_t hold_area = {
        .type = MB_PARAM_HOLDING,
        .start_offset = 0,
        .address = (void*)&holding_reg_params,
        .size = sizeof(holding_reg_params)
    };
    err = mbc_slave_set_descriptor(slave_handle, hold_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor (holding) failed: 0x%x", err);
        return err;
    }

    /* 2. Map INPUT REGISTERS (Read Only for Master) */
    mb_register_area_descriptor_t input_area = {
        .type = MB_PARAM_INPUT,
        .start_offset = 0,
        .address = (void*)&input_reg_params,
        .size = sizeof(input_reg_params)
    };
    err = mbc_slave_set_descriptor(slave_handle, input_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor (input) failed: 0x%x", err);
        return err;
    }

    /* 3. Start Modbus stack */
    err = mbc_slave_start(slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "Modbus TCP Slave successfully started on port %d", MODBUS_PORT);
    }
    
    return err;
}

/**
 * @brief Updates Modbus input registers from the global datastore.
 * Applies necessary scaling factors for integer transmission.
 */
void aqm_modbus_update_registers(void) {
    // 1. Pack boolean statuses into a single status word
    input_reg_params.status_word = 0;
    if (aqm_data.status.wifi_connected) {
        input_reg_params.status_word |= MASK_STATUS_WIFI;
    }
    if (aqm_data.status.relay_active) {
        input_reg_params.status_word |= MASK_STATUS_RELAY;
    }

    // 2. Direct copy of raw ADC values (already uint16_t)
    input_reg_params.so2_raw_val  = aqm_data.adc_raw.so2_raw_val;
    input_reg_params.v3v3_raw_val = aqm_data.adc_raw.v3v3_raw_val;
    input_reg_params.v5v_raw_val  = aqm_data.adc_raw.v5v_raw_val;
    input_reg_params.h2s_raw_val  = aqm_data.adc_raw.h2s_raw_val;
    input_reg_params.co_raw_val   = aqm_data.adc_raw.co_raw_val;
    input_reg_params.nh3_raw_val  = aqm_data.adc_raw.nh3_raw_val;
    input_reg_params.no2_raw_val  = aqm_data.adc_raw.no2_raw_val;
    
    // 3. Scale and cast float values to uint16_t
    // Gases: Multiplied by 100 (2 decimal places)
    input_reg_params.so2_ppm = (uint16_t)(aqm_data.gases.so2_ppm * 100.0f);
    input_reg_params.h2s_ppm = (uint16_t)(aqm_data.gases.h2s_ppm * 100.0f);
    input_reg_params.co_ppm  = (uint16_t)(aqm_data.gases.co_ppm * 100.0f);
    input_reg_params.nh3_ppm = (uint16_t)(aqm_data.gases.nh3_ppm * 100.0f);
    input_reg_params.no2_ppm = (uint16_t)(aqm_data.gases.no2_ppm * 100.0f);

    // Voltages: Multiplied by 1000 (mV resolution)
    input_reg_params.v3v3_val = (uint16_t)(aqm_data.status.v3v3_val * 1000.0f);
    input_reg_params.v5v_val  = (uint16_t)(aqm_data.status.v5v_val * 1000.0f);

    // Climate Data: Multiplied by 10
    // Casting to int16_t first handles negative temperatures safely
    input_reg_params.temperature = (uint16_t)((int16_t)(aqm_data.sen55.temperature * 10.0f));
    input_reg_params.humidity    = (uint16_t)(aqm_data.sen55.humidity * 10.0f);

    // PM Data: No scale needed (ug/m3)
    input_reg_params.pm1_0     = (uint16_t)aqm_data.sen55.pm1_0;
    input_reg_params.pm2_5     = (uint16_t)aqm_data.sen55.pm2_5;
    input_reg_params.pm4_0     = (uint16_t)aqm_data.sen55.pm4_0;
    input_reg_params.pm10_0    = (uint16_t)aqm_data.sen55.pm10_0;
    input_reg_params.voc_index = (uint16_t)aqm_data.sen55.voc_index;
    input_reg_params.nox_index = (uint16_t)aqm_data.sen55.nox_index;

    input_reg_params.uptime_sec_hi = (uint16_t)((aqm_data.status.uptime_sec >> 16) & 0xFFFF); // Higher 16 bits
    input_reg_params.uptime_sec_lo = (uint16_t)(aqm_data.status.uptime_sec & 0xFFFF); // Lower 16 bits

}

/**
 * @brief Processes commands incoming from the Modbus Master (Holding Registers)
 * Should be called periodically in the main Modbus or Tasks loop.
 */
void aqm_modbus_process_holding_regs(void) {
    // Check Relay Control Bit
    if (holding_reg_params.control_word & MASK_RELAY_ENABLE) {
        if (!aqm_data.status.relay_active) {
            ESP_LOGI(TAG, "Modbus command: Relay ON");
            gpio_set_level(RELAY_PIN, 1); // Enable this if using real GPIO
            aqm_data.status.relay_active = 1;
        }
    } else {
        if (aqm_data.status.relay_active) {
            ESP_LOGI(TAG, "Modbus command: Relay OFF");
            gpio_set_level(RELAY_PIN, 0); // Enable this if using real GPIO
            aqm_data.status.relay_active = 0;
        }
    }

    // Check LED Control Bit
    if (holding_reg_params.control_word & MASK_LED_CONTROL) {
        // gpio_set_level(LED_PIN, 1);
    } else {
        // gpio_set_level(LED_PIN, 0);
    }
}