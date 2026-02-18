#ifndef MODBUS_REG_H
#define MODBUS_REG_H

#include <stdint.h>

// Bit masks for Control Word
#define MASK_LED_CONTROL    (1 << 0)  // Bit 0: 0x0001
#define MASK_RELAY_ENABLE   (1 << 1)  // Bit 1: 0x0002

typedef struct {
    uint16_t control_word; // Bit 0: LED control, Bit 1: Relay enable
    uint16_t test_val; // Test register for reading/writing
} holding_reg_params_t;



#endif // MODBUS_REG_H