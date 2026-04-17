#ifndef AQM_MICS6814_H
#define AQM_MICS6814_H

#include <stdint.h>
#include "ads1115.h"

void calculate_mics_r(uint16_t mics_adc_raw, ads1115_pga_t pga, uint16_t voltage_in_mv, uint32_t r_load, uint32_t *out_r);

void aqm_calculate_data_from_mics(void);

#endif // AQM_MICS6814_H