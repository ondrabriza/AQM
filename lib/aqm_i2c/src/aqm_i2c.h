#ifndef AQM_I2C_H
#define AQM_I2C_H

#include <stdint.h>
#include <driver/i2c.h>
#include <esp_err.h>
#include <esp_log.h>


#define I2C_MASTER_NUM              I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000  // 100kHz
#define I2C_MASTER_TIMEOUT_MS       100

/**
 * Initialize all hard- and software components that are needed for the I2C
 * communication.
 */
esp_err_t i2c_init(void);

/**
 * Release all resources initialized by i2c_init().
 */
esp_err_t i2c_free(void);

/**
 * Execute one read transaction on the I2C bus, reading a given number of bytes.
 * If the device does not acknowledge the read command, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to read from
 * @param data    pointer to the buffer where the data is to be stored
 * @param count   number of bytes to read from I2C and store in the buffer
 * @returns 0 on success, error code otherwise
 */
esp_err_t i2c_read(uint8_t address, uint8_t* data, uint16_t count);

/**
 * Execute one write transaction on the I2C bus, sending a given number of
 * bytes. The bytes in the supplied buffer must be sent to the given address. If
 * the slave device does not acknowledge any of the bytes, an error shall be
 * returned.
 *
 * @param address 7-bit I2C address to write to
 * @param data    pointer to the buffer containing the data to write
 * @param count   number of bytes to read from the buffer and send over I2C
 * @returns 0 on success, error code otherwise
 */
esp_err_t i2c_write(uint8_t address, const uint8_t* data, uint16_t count);



#endif /* AQM_I2C_H */
