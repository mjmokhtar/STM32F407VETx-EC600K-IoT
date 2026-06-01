/*
 * sensor.c
 *
 * XY-MD02 Temperature & Humidity Sensor Driver
 * STM32F407VET6 — USART2 (PA2=TX, PA3=RX), RS485, Modbus RTU
 *
 * Modbus RTU request:
 *   01 04 00 01 00 02 20 0B
 *   [addr][func][reg_hi][reg_lo][qty_hi][qty_lo][crc_lo][crc_hi]
 *
 * Response:
 *   01 04 04 [temp_hi][temp_lo][humi_hi][humi_lo][crc_lo][crc_hi]
 *   temp = (temp_hi<<8 | temp_lo) / 10.0
 *   humi = (humi_hi<<8 | humi_lo) / 10.0
 */

#include "sensor.h"
#include <string.h>

/* ── Last valid data ─────────────────────────────────────────── */
static sensor_data_t _last = {0};

/* ── Modbus request frame (fixed, pre-calculated CRC) ────────── */
static const uint8_t _request[8] = {
    SENSOR_DEVICE_ADDR, /* 0x01 device address */
    0x04,               /* function code: read input registers */
    0x00,               /* start address high */
    0x01,               /* start address low */
    0x00,               /* quantity high */
    0x02,               /* quantity low (2 register: temp + humi) */
    0x20,               /* CRC low */
    0x0B                /* CRC high */
};

/* ── CRC16 Modbus ────────────────────────────────────────────── */
static uint16_t _crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ═══════════════════════════════════════════════════════════════
 * sensor_init
 * ═══════════════════════════════════════════════════════════════ */
void sensor_init(void)
{
    /* USART2 sudah di-init oleh CubeMX generated MX_USART2_UART_Init()  */
    /* Tidak perlu inisialisasi tambahan                                  */
    memset(&_last, 0, sizeof(_last));
}

/* ═══════════════════════════════════════════════════════════════
 * sensor_read
 * Baca temperature dan humidity dari XY-MD02
 * Returns: SENSOR_OK, SENSOR_TIMEOUT, atau SENSOR_CRC_ERROR
 * ═══════════════════════════════════════════════════════════════ */
int sensor_read(sensor_data_t *out)
{
    uint8_t response[9];
    uint8_t dummy;

    /* Flush RX buffer sampai benar-benar kosong */
    while (HAL_UART_Receive(&SENSOR_UART, &dummy, 1, 2) == HAL_OK) {}

    /* Kirim request */
    HAL_UART_Transmit(&SENSOR_UART,
                      (uint8_t *)_request,
                      sizeof(_request),
                      200);

    /* Tunggu lebih lama — beri waktu modul RS485 switching */
    HAL_Delay(100);

    /* Flush lagi — buang echo dari TX */
    while (HAL_UART_Receive(&SENSOR_UART, &dummy, 1, 2) == HAL_OK) {}

    /* Terima response */
    memset(response, 0, sizeof(response));
    HAL_StatusTypeDef status = HAL_UART_Receive(&SENSOR_UART,
                                                 response, 9,
                                                 500);

    if (status != HAL_OK)
    {
        _last.valid = false;
        if (out) *out = _last;
        return SENSOR_TIMEOUT;
    }

    if (response[1] != 0x04)
    {
        _last.valid = false;
        if (out) *out = _last;
        return SENSOR_TIMEOUT;
    }

    /* Validasi CRC */
    uint16_t crc_calc = _crc16(response, 7);
    uint16_t crc_recv = (uint16_t)(response[8] << 8) | response[7];
    if (crc_calc != crc_recv)
    {
        _last.valid = false;
        if (out) *out = _last;
        return SENSOR_CRC_ERROR;
    }

    /* Parse — dibagi 100 sesuai datasheet */
    _last.temperature = ((uint16_t)(response[3] << 8) | response[4]) / 100.0f;
    _last.humidity    = ((uint16_t)(response[5] << 8) | response[6]) / 100.0f;
    _last.valid       = true;

    if (out) *out = _last;
    return SENSOR_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * sensor_get_last
 * Ambil data terakhir tanpa baca ulang sensor
 * ═══════════════════════════════════════════════════════════════ */
void sensor_get_last(sensor_data_t *out)
{
    if (out) *out = _last;
}
