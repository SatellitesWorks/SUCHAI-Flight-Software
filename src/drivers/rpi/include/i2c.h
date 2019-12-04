/**
 * @file  i2c.h
 * @author Gustavo Díaz H - g.hernan.diaz@ing.uchile.cl
 * @date 2019
 * @copyright GNU GPL v3
 *
 * This header have definitions of commands related to i2c interface for Linux
 * (RW) features.
 */

#ifndef I2C_H
#define I2C_H

//#include "osDelay.h"

#include <stdint.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define BIuC_ADDR 0x11

/**
 * Register reaction whee related (rw) commands
 */

int i2c_write(uint8_t addr, uint8_t data1, uint8_t data2, uint8_t data3);

/**
 * i2c write master transaction to slave
 *
 * @param addr Slave address to write to
 * @param data1 first byte to send
 * @param data2 second byte to send
 * @param data3 third byte to send
 * @return  CMD_OK if executed correctly or CMD_FAIL in case of errors
 */

int i2c_read(char* buf);

/**
 * i2c read master transaction to slave
 *
 * @param buf registers to save data from slave
 * @return  CMD_OK if executed correctly or CMD_FAIL in case of errors
 */


int8_t i2c_write_n(uint8_t addr, uint8_t reg_addr, uint8_t *reg_data, uint16_t len);

/*!
 *    @brief  Low level generic i2c write
 *    @param  addr, reg_addr, reg_data, len
 *    @return err_code
 */

int8_t i2c_read_n(uint8_t dev_id, uint8_t reg_addr, uint8_t *reg_data, uint16_t len, uint8_t delay_ms);

/*!
 *    @brief  Low level generic i2c read
 *    @param  addr, reg_addr, reg_data, len
 *    @return err_code
 */

int8_t i2c_read_from_n(uint8_t dev_id, uint8_t *reg_data, uint8_t len);

/*!
 *    @brief  Low level specific i2c write
 *    @param  addr, data
 *    @return err_code
 */

int8_t i2c_write_addr(uint8_t addr, uint8_t data);

#endif /* CMD_RW_H */