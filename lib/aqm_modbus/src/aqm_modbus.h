#ifndef AQM_MODBUS_H
#define AQM_MODBUS_H
#include <esp_err.h>

esp_err_t aqm_init_modbus();

void aqm_modbus_update_registers(void);
void aqm_modbus_process_holding_regs(void);

#endif // AQM_MODBUS_H

