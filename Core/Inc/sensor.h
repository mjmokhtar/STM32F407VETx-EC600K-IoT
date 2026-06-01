/*
 * sensor.h
 *
 * XY-MD02 Temperature & Humidity Sensor Driver
 * STM32F407VET6 — USART2 (PA2=TX, PA3=RX), RS485, Modbus RTU
 */

#ifndef SENSOR_H
#define SENSOR_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ── USART Handle ────────────────────────────────────────────── */
extern UART_HandleTypeDef huart2;

/* ── Config ──────────────────────────────────────────────────── */
#define SENSOR_UART         huart2
#define SENSOR_TIMEOUT_MS   500
#define SENSOR_DEVICE_ADDR  0x01    /* default Modbus address XY-MD02 */

/* ── Error codes ─────────────────────────────────────────────── */
#define SENSOR_OK           0
#define SENSOR_TIMEOUT     -1
#define SENSOR_CRC_ERROR   -2

/* ── Data struct ─────────────────────────────────────────────── */
typedef struct {
    float temperature;  /* derajat Celsius */
    float humidity;     /* persen RH */
    bool  valid;        /* true jika data terakhir berhasil dibaca */
} sensor_data_t;

/* ── Public API ──────────────────────────────────────────────── */
void sensor_init(void);
int  sensor_read(sensor_data_t *out);
void sensor_get_last(sensor_data_t *out);

#endif /* SENSOR_H */
