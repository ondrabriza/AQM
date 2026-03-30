#include "aqm_modbus.h"
#include "aqm_modbus_reg.h"
#include "aqm_datastore.h"
#include "aqm_config.h"

#include <mbcontroller.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_netif.h>


static const char *TAG = "AQM_MODBUS";

#define MODBUS_PORT 502

// Global instances of Modbus registers mapped to memory
holding_reg_params_t holding_reg_params = {0};
input_reg_params_t input_reg_params = {0};

// Handle for the Modbus stack
static void* tcp_slave_handle = NULL;
static void* rtu_slave_handle = NULL; // Separate handle for RTU if needed
static uint16_t s_last_control_word = 0;

/**
 * @brief Initializes Modbus TCP Slave and maps memory areas
 * @return esp_err_t ESP_OK on success
 */
esp_err_t aqm_init_modbus_tcp(void) {
    mb_communication_info_t comm_info = {0};
    
    /* Configure TCP options for the slave */
    comm_info.tcp_opts.port = MODBUS_PORT;
    comm_info.tcp_opts.mode = MB_TCP;
    comm_info.tcp_opts.addr_type = MB_IPV4;
    comm_info.tcp_opts.ip_addr_table = NULL; 
    comm_info.tcp_opts.uid = 1;              

    /* -----------------------------------------------------------------
     * OPRAVA: Dynamické vyhledání aktivního síťového rozhraní pro mDNS
     * ----------------------------------------------------------------- */
    esp_netif_t *active_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    
    // Zkontrolujeme, zda má STA (klient) přidělenou IP adresu
    if (esp_netif_get_ip_info(active_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        // STA nemá IP. Zkusíme AP rozhraní (Fallback Web Server)
        active_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }
    
    // Nyní předáme Modbusu to rozhraní, které aktuálně funguje
    comm_info.tcp_opts.ip_netif_ptr = (void *)active_netif;
    /* ----------------------------------------------------------------- */

    esp_err_t err = mbc_slave_create_tcp(&comm_info, &tcp_slave_handle);
    if (err != ESP_OK || tcp_slave_handle == NULL) {
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
    err = mbc_slave_set_descriptor(tcp_slave_handle, hold_area);
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
    err = mbc_slave_set_descriptor(tcp_slave_handle, input_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor (input) failed: 0x%x", err);
        return err;
    }

    /* 3. Start Modbus stack */
    err = mbc_slave_start(tcp_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "Modbus TCP Slave successfully started on port %d", MODBUS_PORT);
    }
    
    return err;
}

/**
 * @brief Initializes Modbus RTU (Serial RS485) Slave and maps memory areas
 * @return esp_err_t ESP_OK on success
 */
esp_err_t aqm_init_modbus_rtu(void) {
    
    mb_communication_info_t comm_info = {0};

    /* Configure Serial options for the RTU slave using ser_opts struct */
    comm_info.ser_opts.mode = MB_RTU;
    comm_info.ser_opts.port = UART_NUM_1;                  /* Use UART1 (can be adjusted) */
    comm_info.ser_opts.uid = 1;                            /* Modbus Unit ID (Slave ID) */
    comm_info.ser_opts.baudrate = 9600;                    /* Default baud rate */
    comm_info.ser_opts.data_bits = UART_DATA_8_BITS;       /* 8 data bits (standard for Modbus RTU) */
    comm_info.ser_opts.stop_bits = UART_STOP_BITS_1;       /* 1 stop bit */
    comm_info.ser_opts.parity = UART_PARITY_DISABLE;       /* No parity */

    esp_err_t err = mbc_slave_create_serial(&comm_info, &rtu_slave_handle);
    if (err != ESP_OK || rtu_slave_handle == NULL) {
        ESP_LOGE(TAG, "mbc_slave_create_serial failed: 0x%x", err);
        return err;
    }

    /* Configure UART pins for RS485 */
    /* RS485_DE_PIN acts as RTS and handles both DE and RE since they are connected together */
    err = uart_set_pin(UART_NUM_1, RS485_D_PIN, RS485_R_PIN, RS485_DE_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: 0x%x", err);
        return err;
    }

    /* Set UART mode to RS485 Half Duplex (automatically controls RTS pin) */
    err = uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode failed: 0x%x", err);
        return err;
    }

    /* 1. Map HOLDING REGISTERS (Read/Write) */
    mb_register_area_descriptor_t hold_area = {
        .type = MB_PARAM_HOLDING,
        .start_offset = 0,
        .address = (void*)&holding_reg_params,
        .size = sizeof(holding_reg_params)
    };
    err = mbc_slave_set_descriptor(rtu_slave_handle, hold_area);
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
    err = mbc_slave_set_descriptor(rtu_slave_handle, input_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor (input) failed: 0x%x", err);
        return err;
    }

    /* 3. Start Modbus stack */
    err = mbc_slave_start(rtu_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "Modbus RTU Slave successfully started on UART 1, Baud: %lu", comm_info.ser_opts.baudrate);
    }
    
    return err;
}


/**
 * @brief Updates Modbus input registers from the global datastore.
 * Synchronize holding registers with datastore and vice versa
 * Applies necessary scaling factors for integer transmission.
 */
void aqm_modbus_update_registers(void) {
    // ==========================================
    // 1. UPDATE INPUT REGISTERS (Read Only)
    // ==========================================
    
    // State bits for status word (Bit 0=WiFi, Bit 1=Relay, Bit 2=LED)
    input_reg_params.status_word = 0;
    if (aqm_data.status.status_word.flags.wifi_en) {
        input_reg_params.status_word |= MASK_STATUS_WIFI;
    }
    if (aqm_data.status.status_word.flags.relay_state) {
        input_reg_params.status_word |= MASK_STATUS_RELAY;
    }
    if (aqm_data.status.status_word.flags.led_state) {
        input_reg_params.status_word |= MASK_STATUS_LED; 
    }

    // Copy ADC values
    input_reg_params.so2_raw_val  = aqm_data.adc_raw.so2_raw_val;
    input_reg_params.v3v3_raw_val = aqm_data.adc_raw.v3v3_raw_val;
    input_reg_params.v5v_raw_val  = aqm_data.adc_raw.v5v_raw_val;
    input_reg_params.h2s_raw_val  = aqm_data.adc_raw.h2s_raw_val;
    input_reg_params.co_raw_val   = aqm_data.adc_raw.co_raw_val;
    input_reg_params.nh3_raw_val  = aqm_data.adc_raw.nh3_raw_val;
    input_reg_params.no2_raw_val  = aqm_data.adc_raw.no2_raw_val;
        
    // Scale gas sensor values (PPM * 100)
    input_reg_params.so2_ppm = (uint16_t)(aqm_data.gases.so2_ppm * 100.0f);
    input_reg_params.h2s_ppm = (uint16_t)(aqm_data.gases.h2s_ppm * 100.0f);
    input_reg_params.co_mv   = (uint16_t)(aqm_data.gases.co_mv);
    input_reg_params.nh3_mv  = (uint16_t)(aqm_data.gases.nh3_mv);
    input_reg_params.no2_mv  = (uint16_t)(aqm_data.gases.no2_mv);

    // Scale voltage values (mV)
    input_reg_params.v3v3_val = aqm_data.status.v3v3_val;
    input_reg_params.v5v_val  = aqm_data.status.v5v_val;

    // Climate data (Values * 10 for one decimal place)
    input_reg_params.temperature = (uint16_t)(aqm_data.sen55.temperature);
    input_reg_params.humidity    = (uint16_t)(aqm_data.sen55.humidity);

    // Particle data and indices
    input_reg_params.pm1_0     = aqm_data.sen55.pm1_0;
    input_reg_params.pm2_5     = aqm_data.sen55.pm2_5;
    input_reg_params.pm4_0     = aqm_data.sen55.pm4_0;
    input_reg_params.pm10_0    = aqm_data.sen55.pm10_0;
    input_reg_params.voc_index = (uint16_t)aqm_data.sen55.voc_index;
    input_reg_params.nox_index = (uint16_t)aqm_data.sen55.nox_index;

    // 32-bit Uptime (Big-Endian formát)
    input_reg_params.uptime_sec_hi = (uint16_t)((aqm_data.status.timestamp >> 16) & 0xFFFF);
    input_reg_params.uptime_sec_lo = (uint16_t)(aqm_data.status.timestamp & 0xFFFF);


    // ==========================================
    // 2. Two-way synchronization for Control Word
    // ==========================================
    
    // Master wrote to HR
    if (holding_reg_params.control_word != s_last_control_word) {
        
        ESP_LOGI(TAG, "Modbus wrote new control word: 0x%04X", holding_reg_params.control_word);
        
        // data flow: Modbus HR -> Global Datastore 
        aqm_data.control_word.word = holding_reg_params.control_word;
        
        s_last_control_word = holding_reg_params.control_word;
        
        aqm_modbus_set_flag_cw_changed();
    }
    
    // Datastore changed - Datastore -> Modbus HR
    else if (aqm_data.control_word.word != s_last_control_word) {
        
        ESP_LOGI(TAG, "CW changed from Datastore: 0x%04X", aqm_data.control_word.word);
        
        
        holding_reg_params.control_word = aqm_data.control_word.word;
        
        
        s_last_control_word = aqm_data.control_word.word;
        
        aqm_modbus_set_flag_cw_changed();

    }
}

void aqm_modbus_set_flag_cw_changed(void){

    // Set the cw_changed flag in the control word to indicate that it was changed
    aqm_data.control_word.flags.cw_changed = 1;
}