#include "aqm_modbus.h"
//#include "aqm_modbus_reg.h"
#include "aqm_datastore.h"
#include "aqm_config.h"

#include <mbcontroller.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_netif.h>


static const char *TAG = "AQM_MODBUS";

#define MODBUS_PORT 502
#define MB_RTU_BAUD_RATE 115200

// Global instances of Modbus registers mapped to memory
/*holding_reg_params_t holding_reg_params = {0};
input_reg_params_t input_reg_params = {0};*/

// Handle for the Modbus stack
static void* tcp_slave_handle = NULL;
static void* rtu_slave_handle = NULL; // Separate handle for RTU if needed
//static uint16_t s_last_control_word = 0;


static uint16_t next_holding_offset = 0;
static uint16_t next_input_offset = 0;

/**
 * @brief Register a block of registers with Modbus
 */
static esp_err_t aqm_register_modbus_block(void* slave_handle, mb_param_type_t type, void* address, size_t size_bytes) {

    mb_register_area_descriptor_t area = {
        .type = type,
        .address = address,
        .size = size_bytes
    };

    uint16_t registers_count = size_bytes / 2; 

    if (type == MB_PARAM_HOLDING) {
        area.start_offset = next_holding_offset;
        next_holding_offset += registers_count;
    } 
    else if (type == MB_PARAM_INPUT) {
        area.start_offset = next_input_offset;
        next_input_offset += registers_count; 
    } 
    else {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mbc_slave_set_descriptor(slave_handle, area);
    if (err != ESP_OK) {
        ESP_LOGE("MODBUS", "Failed to register block at offset %d", area.start_offset);
    }
    return err;
}

/**
 * @brief Sets up the descriptors for the Modbus slave
 * 
 * @param slave_handle 
 * @return esp_err_t 
 */
static esp_err_t aqm_modbus_setup_descriptors(void* slave_handle){

    esp_err_t err = ESP_OK;
    next_holding_offset = 0;
    next_input_offset = 0;

    // Map holding registers (read/write)
    err = aqm_register_modbus_block(slave_handle, MB_PARAM_HOLDING, &aqm_data.config, sizeof(aqm_data.config));
    if (err != ESP_OK) return err;

    // Map input registers (read only)
    err= aqm_register_modbus_block(slave_handle, MB_PARAM_INPUT, &aqm_data.data, sizeof(aqm_data.data));
    if (err != ESP_OK) return err;


    return err;
}



/**
 * @brief Initializes Modbus TCP Slave and maps memory areas
 * @return esp_err_t ESP_OK on success
 */
esp_err_t aqm_init_modbus_tcp(void) {
    mb_communication_info_t comm_info = {0};
    
    // Configure TCP options
    comm_info.tcp_opts.port = MODBUS_PORT;
    comm_info.tcp_opts.mode = MB_TCP;
    comm_info.tcp_opts.addr_type = MB_IPV4;
    comm_info.tcp_opts.ip_addr_table = NULL; 
    comm_info.tcp_opts.uid = 1;              

    // Dynamic search for active network interface for mDNS
    esp_netif_t *active_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    
    // Check if STA (client) has an assigned IP address
    if (esp_netif_get_ip_info(active_netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        // STA has no IP. Let's try AP interface (Fallback Web Server)
        active_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    }
    
    comm_info.tcp_opts.ip_netif_ptr = (void *)active_netif;

    esp_err_t err = mbc_slave_create_tcp(&comm_info, &tcp_slave_handle);
    if (err != ESP_OK || tcp_slave_handle == NULL) {
        ESP_LOGE(TAG, "mbc_slave_create_tcp failed: 0x%x", err);
        return err;
    }

    ESP_ERROR_CHECK(aqm_modbus_setup_descriptors(tcp_slave_handle));


    /* 3. Start Modbus stack */
    err = mbc_slave_start(tcp_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "Modbus TCP Slave successfully started on port %d", MODBUS_PORT);
    }
    
    return err;
}

// Init Modbus RTU
esp_err_t aqm_init_modbus_rtu(void) {
    
    mb_communication_info_t comm_info = {0};

    // Configure serial options for RTU
    comm_info.ser_opts.mode = MB_RTU;
    comm_info.ser_opts.port = UART_NUM_1;
    comm_info.ser_opts.uid = 1;
    comm_info.ser_opts.baudrate = MB_RTU_BAUD_RATE;
    comm_info.ser_opts.data_bits = UART_DATA_8_BITS;
    comm_info.ser_opts.stop_bits = UART_STOP_BITS_1;
    comm_info.ser_opts.parity = UART_PARITY_DISABLE;

    esp_err_t err = mbc_slave_create_serial(&comm_info, &rtu_slave_handle);
    if (err != ESP_OK || rtu_slave_handle == NULL) {
        ESP_LOGE(TAG, "mbc_slave_create_serial failed: 0x%x", err);
        return err;
    }

    // Configure RS485 UART pins
    err = uart_set_pin(UART_NUM_1, RS485_D_PIN, RS485_R_PIN, RS485_DE_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: 0x%x", err);
        return err;
    }

    // Set RS485 half-duplex mode
    err = uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_mode failed: 0x%x", err);
        return err;
    }

    ESP_ERROR_CHECK(aqm_modbus_setup_descriptors(rtu_slave_handle));

    /* 3. Start Modbus stack */
    err = mbc_slave_start(rtu_slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    } else {
        ESP_LOGI(TAG, "Modbus RTU Slave successfully started on UART 1, Baud: %lu", comm_info.ser_opts.baudrate);
    }
    
    return err;
}


/*
// Update Modbus registers (commented out)
void aqm_modbus_update_registers(void) {
    // ==========================================
    // 1. UPDATE INPUT REGISTERS (Read Only)
    // ==========================================
    
    // State bits for status word (Bit 0=WiFi, Bit 1=Relay, Bit 2=LED)
    //input_reg_params.status_word = aqm_data.status.status_word.word;
    //if(aqm_data.status.status_word.flags.measure_en) {
    //    input_reg_params.status_word |= MASK_MEASURE_EN;
    //}
    //if (aqm_data.status.status_word.flags.wifi_en) {
    //    input_reg_params.status_word |= MASK_WIFI_EN;
    //}
    //if (aqm_data.status.status_word.flags.relay_state) {
    //    input_reg_params.status_word |= MASK_RELAY_STATE;
    //}
    //if (aqm_data.status.status_word.flags.led_state) {
    //    input_reg_params.status_word |= MASK_LED_STATE; 
    //}
    

    // Copy ADC values
    input_reg_params.so2_raw_val  = aqm_data.adc_raw.so2_raw_val;
    input_reg_params.v3v3_raw_val = aqm_data.adc_raw.v3v3_raw_val;
    input_reg_params.v5v_raw_val  = aqm_data.adc_raw.v5v_raw_val;
    input_reg_params.h2s_raw_val  = aqm_data.adc_raw.h2s_raw_val;
    input_reg_params.mics_red_raw_val   = aqm_data.adc_raw.mics_red_raw_val;
    input_reg_params.mics_nh3_raw_val  = aqm_data.adc_raw.mics_nh3_raw_val;
    input_reg_params.mics_ox_raw_val  = aqm_data.adc_raw.mics_ox_raw_val;
        
    // Scale gas sensor values (PPM * 100)
    input_reg_params.so2_ppm = (uint16_t)(aqm_data.gases.so2_ppm * 100.0f);
    input_reg_params.h2s_ppm = (uint16_t)(aqm_data.gases.h2s_ppm * 100.0f);
    input_reg_params.mics_ox_r   = (uint16_t)(aqm_data.gases.mics_ox_r);
    input_reg_params.mics_nh3_r  = (uint16_t)(aqm_data.gases.mics_nh3_r);
    input_reg_params.mics_red_r  = (uint16_t)(aqm_data.gases.mics_red_r);

    // Scale voltage values (mV)
    input_reg_params.v3v3_mv = aqm_data.status.v3v3_mv;
    input_reg_params.v5v_mv  = aqm_data.status.v5v_mv;

    // Climate data (Temperature in C * 200, Humidity in % * 100)
    input_reg_params.temperature = (uint16_t)(aqm_data.sen55.temperature);
    input_reg_params.humidity    = (uint16_t)(aqm_data.sen55.humidity);

    // Particle data and indices
    input_reg_params.pm1_0     = aqm_data.sen55.pm1_0;
    input_reg_params.pm2_5     = aqm_data.sen55.pm2_5;
    input_reg_params.pm4_0     = aqm_data.sen55.pm4_0;
    input_reg_params.pm10_0    = aqm_data.sen55.pm10_0;
    input_reg_params.voc_index = (uint16_t)aqm_data.sen55.voc_index;
    input_reg_params.nox_index = (uint16_t)aqm_data.sen55.nox_index;

    // 32-bit Uptime (Big-Endian format)
    input_reg_params.uptime_sec_hi = (uint16_t)((aqm_data.status.timestamp >> 16) & 0xFFFF);
    input_reg_params.uptime_sec_lo = (uint16_t)(aqm_data.status.timestamp & 0xFFFF);


    // ==========================================
    // 2. Two-way synchronization for Control Word
    // ==========================================
    
    // Master wrote to HR
    if (holding_reg_params.control_word != s_last_control_word) {
        
        //ESP_LOGI(TAG, "Modbus wrote new control word: 0x%04X", holding_reg_params.control_word);
        
        // data flow: Modbus HR -> Global Datastore 
        aqm_data.control_word.word = holding_reg_params.control_word;
        
        s_last_control_word = holding_reg_params.control_word;
        
        aqm_datastore_set_flag_cw_changed();
    }
    
    // Datastore changed - Datastore -> Modbus HR
    else if (aqm_data.control_word.word != s_last_control_word) {
        
        //ESP_LOGI(TAG, "CW changed from Datastore: 0x%04X", aqm_data.control_word.word);
        
        
        holding_reg_params.control_word = aqm_data.control_word.word;
        
        
        s_last_control_word = aqm_data.control_word.word;
        
        aqm_datastore_set_flag_cw_changed();

    }
}*/

