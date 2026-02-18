#include "aqm_modbus.h"
#include "modbus_reg.h"
#include "mbcontroller.h"

#include <esp_wifi.h>
#include <esp_log.h>
#include "driver/gpio.h"
static const char *TAG = "AQM_MODBUS";

#define MODBUS_PORT     502
static esp_netif_t *s_sta_netif = NULL;

holding_reg_params_t hold_reg_data;


// --- MODBUS INITIALIZATION ---
esp_err_t aqm_init_modbus() {
    mb_communication_info_t comm_info = {0};
    /* Configure TCP options for the slave */
    comm_info.tcp_opts.port = MODBUS_PORT;
    comm_info.tcp_opts.mode = MB_TCP;
    comm_info.tcp_opts.addr_type = MB_IPV4;
    comm_info.tcp_opts.ip_addr_table = NULL; /* bind to any address */
    comm_info.tcp_opts.ip_netif_ptr = (void *)s_sta_netif;
    comm_info.tcp_opts.uid = 1; /* Modbus unit id */

    void* slave_handle = NULL;
    esp_err_t err = mbc_slave_create_tcp(&comm_info, &slave_handle);
    if (err != ESP_OK || slave_handle == NULL) {
        ESP_LOGE(TAG, "mbc_slave_create_tcp failed: 0x%x", err);
        return err;
    }

    mb_register_area_descriptor_t hold_area = {0};
    hold_area.type = MB_PARAM_HOLDING;
    hold_area.start_offset = 0;
    hold_area.address = (void*)&hold_reg_data;
    hold_area.size = sizeof(hold_reg_data);
    hold_area.access = MB_ACCESS_RW;
    err = mbc_slave_set_descriptor(slave_handle, hold_area);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_set_descriptor (holding) failed: 0x%x", err);
        return err;
    }

    /* Start Modbus stack */
    err = mbc_slave_start(slave_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mbc_slave_start failed: 0x%x", err);
    }
    return err;
}
