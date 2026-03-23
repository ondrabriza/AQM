#ifndef AQM_MODBUS_H
#define AQM_MODBUS_H
#include <esp_err.h>

esp_err_t aqm_init_modbus_tcp(void);
esp_err_t aqm_init_modbus_rtu(void);

void aqm_modbus_update_registers(void);
void aqm_modbus_set_flag_cw_changed(void);

#endif // AQM_MODBUS_H

