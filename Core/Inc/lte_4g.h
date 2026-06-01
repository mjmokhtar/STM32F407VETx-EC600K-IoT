/*
 * lte_4g.h
 * EC600K-CN 4G LTE — MQTT Publish
 * STM32F407VET6 — USART3 (PB10=TX, PB11=RX)
 */

#ifndef LTE_4G_H
#define LTE_4G_H

#include "stm32f4xx_hal.h"
#include "sensor.h"
#include "cmsis_os.h"
#include <stdint.h>
#include <stdbool.h>

extern UART_HandleTypeDef huart1;

#define LTE_UART                huart1
#define LTE_UART_BUF_SIZE       512

#define AT_TIMEOUT_SHORT        3000
#define AT_TIMEOUT_MEDIUM       10000
#define AT_TIMEOUT_LONG         60000

#define LTE_APN                 "internet"

#define MQTT_HOST               "72.62.124.206"
#define MQTT_PORT               "1883"
#define MQTT_CLIENT_ID          "STM32-001"
#define MQTT_USER               ""
#define MQTT_PASS               ""
#define MQTT_PUB_TOPIC          "STM32-001/env"
#define MQTT_SUB_TOPIC          "STM32-001/cmd"
#define MQTT_PUBLISH_INTERVAL_MS 10000

/* Status struct — untuk JSON */
typedef struct {
    int   rssi;
    int   rsrp;
    int   sinr;
    char  operator_name[32];
    char  band[16];
    char  datetime[32];
    bool  time_synced;
} lte_status_t;

void lte_4g_init(void);
void lte_4g_get_status(lte_status_t *out);

#endif /* LTE_4G_H */

/* Dipanggil dari HAL_UART_RxCpltCallback di main.c */
void lte_uart_rx_callback(void);
