/*
 * lte_4g.c
 * EC600K-CN 4G LTE — MQTT Publish
 * STM32F407VET6 — USART1 (PA9=TX, PA10=RX)
 *
 * Fix utama: UART RX menggunakan interrupt + ring buffer
 * sehingga tidak ada byte yang hilang akibat FreeRTOS preemption
 */

#include "lte_4g.h"
#include <string.h>
#include <stdio.h>

/* ── Task attribute ───────────────────────────────────────────── */
const osThreadAttr_t lte_task_attr = {
    .name       = "lte_task",
    .stack_size = 1024 * 4,
    .priority   = osPriorityNormal,
};

/* ── Shared status ────────────────────────────────────────────── */
static lte_status_t g_status;
static osMutexId_t  g_mutex;

/* ── Ring buffer untuk UART RX interrupt ─────────────────────── */
#define RX_BUF_SIZE  1024
static volatile uint8_t  _rx_buf[RX_BUF_SIZE];
static volatile uint16_t _rx_head = 0;
static volatile uint16_t _rx_tail = 0;
static uint8_t           _rx_byte;  /* buffer 1 byte untuk HAL IT */

/* ── Fungsi ring buffer ──────────────────────────────────────── */
static void _rx_start(void)
{
    HAL_UART_Receive_IT(&LTE_UART, &_rx_byte, 1);
}

/* Dipanggil dari HAL_UART_RxCpltCallback di main.c */
void lte_uart_rx_callback(void)
{
    uint16_t next = (_rx_head + 1) % RX_BUF_SIZE;
    if (next != _rx_tail)
    {
        _rx_buf[_rx_head] = _rx_byte;
        _rx_head = next;
    }
    HAL_UART_Receive_IT(&LTE_UART, &_rx_byte, 1);
}

static int _rx_available(void)
{
    return (_rx_head - _rx_tail + RX_BUF_SIZE) % RX_BUF_SIZE;
}

static uint8_t _rx_read(void)
{
    uint8_t c = _rx_buf[_rx_tail];
    _rx_tail = (_rx_tail + 1) % RX_BUF_SIZE;
    return c;
}

static void _rx_flush(void)
{
    _rx_tail = _rx_head;
}

/* ── Debug print via USART3 ──────────────────────────────────── */
static void _debug(const char *msg)
{
    extern UART_HandleTypeDef huart3;
    HAL_UART_Transmit(&huart3, (uint8_t *)msg, strlen(msg), 100);
}

/* ── Kirim ke EC600K ─────────────────────────────────────────── */
static void _tx(const char *cmd, bool newline)
{
    if (newline)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s\r\n", cmd);
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)buf, strlen(buf), 1000);
    }
    else
    {
        HAL_UART_Transmit(&LTE_UART, (uint8_t *)cmd, strlen(cmd), 2000);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * _sendWait — kirim command, tunggu waitString
 * Baca dari ring buffer (tidak ada byte yang hilang)
 * ═══════════════════════════════════════════════════════════════ */
static bool _sendWait(const char *cmd, const char *waitStr,
                      uint32_t timeout, bool newline)
{
    char     buf[LTE_UART_BUF_SIZE] = {0};
    int      total = 0;
    uint32_t start;
    char     dbg[160];

    /* Flush buffer sebelum kirim */
    _rx_flush();

    /* Kirim */
    _tx(cmd, newline);

    if (newline)
    {
        snprintf(dbg, sizeof(dbg), "[LTE] >> %s\r\n", cmd);
        _debug(dbg);
    }
    else
    {
        snprintf(dbg, sizeof(dbg), "[LTE] >> [payload %d bytes]\r\n",
                 (int)strlen(cmd));
        _debug(dbg);
    }

    /* Baca dari ring buffer sampai waitStr ditemukan atau timeout */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;

            if (waitStr && strstr(buf, waitStr))
            {
                snprintf(dbg, sizeof(dbg), "[LTE] << OK (%s)\r\n", waitStr);
                _debug(dbg);
                return true;
            }
        }
        osDelay(1);
    }

    /* Debug — print isi buffer saat timeout */
    snprintf(dbg, sizeof(dbg), "[LTE] TIMEOUT (%s)\r\n",
             waitStr ? waitStr : "");
    _debug(dbg);

    /* Print raw response untuk debug */
    _debug("[LTE] RAW: [");
    for (int i = 0; i < total && i < 200; i++)
    {
        char h[5];
        uint8_t c = (uint8_t)buf[i];
        if (c >= 32 && c < 127)
            snprintf(h, sizeof(h), "%c", c);
        else
            snprintf(h, sizeof(h), "<%02X>", c);
        _debug(h);
    }
    _debug("]\r\n");

    return false;
}

/* ═══════════════════════════════════════════════════════════════
 * _moduleReady — AT+QINISTAT retry 60x
 * ═══════════════════════════════════════════════════════════════ */
static bool _moduleReady(void)
{
    _debug("[LTE] Checking module...\r\n");
    for (int i = 0; i < 60; i++)
    {
        if (_sendWait("AT+QINISTAT", "+QINISTAT: 1", 1000, true))
        {
            _debug("[LTE] Module ready\r\n");
            return true;
        }
        char dbg[32];
        snprintf(dbg, sizeof(dbg), "[LTE] Wait %d/60\r\n", i + 1);
        _debug(dbg);
        osDelay(1000);
    }
    _debug("[LTE] Module not responding\r\n");
    return false;
}

/* ═══════════════════════════════════════════════════════════════
 * read functions
 * ═══════════════════════════════════════════════════════════════ */
static void read_signal_quality(void)
{
    char     buf[LTE_UART_BUF_SIZE] = {0};
    int      total = 0;
    uint32_t start;
    char     dbg[128];

    /* ── AT+CSQ ── */
    _rx_flush();
    _tx("AT+CSQ\r\n", false);
    _debug("[LTE] >> AT+CSQ\r\n");

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;
            if (strstr(buf, "OK")) goto csq_done;
        }
        osDelay(1);
    }
csq_done:;
    int rssi = 99, ber = 99;
    char *p = strstr(buf, "+CSQ:");
    if (p) sscanf(p, "+CSQ: %d,%d", &rssi, &ber);

    osMutexAcquire(g_mutex, osWaitForever);
    g_status.rssi = rssi;
    osMutexRelease(g_mutex);

    int dbm = (rssi == 99) ? 0 : (-113 + rssi * 2);
    snprintf(dbg, sizeof(dbg), "[LTE] RSSI=%d (%ddBm)\r\n", rssi, dbm);
    _debug(dbg);

    /* ── AT+QCSQ — rsrp dan sinr ── */
    memset(buf, 0, sizeof(buf));
    total = 0;

    _rx_flush();
    _tx("AT+QCSQ\r\n", false);
    _debug("[LTE] >> AT+QCSQ\r\n");

    /* Response: +QCSQ: "LTE",-85,-95,196,-10 */
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;
            if (strstr(buf, "OK")) goto qcsq_done;
        }
        osDelay(1);
    }
qcsq_done:;
    snprintf(dbg, sizeof(dbg), "[LTE] QCSQ raw: %.80s\r\n", buf);
    _debug(dbg);

    char *q = strstr(buf, "+QCSQ:");
    if (q)
    {
        /* Skip tipe jaringan "LTE", ambil angka setelah koma pertama */
        char *comma = strchr(q, ',');
        if (comma)
        {
            int rsrp = 0, rsrq = 0, sinr = 0, rssnr = 0;
            sscanf(comma + 1, "%d,%d,%d,%d", &rsrp, &rsrq, &sinr, &rssnr);

            osMutexAcquire(g_mutex, osWaitForever);
            g_status.rsrp = rsrp;
            g_status.sinr = sinr / 10; /* sinr dalam 0.1dB unit */
            osMutexRelease(g_mutex);

            snprintf(dbg, sizeof(dbg),
                     "[LTE] RSRP=%d SINR=%d\r\n", rsrp, sinr / 10);
            _debug(dbg);
        }
    }
}

static void read_operator(void)
{
    char buf[LTE_UART_BUF_SIZE] = {0};
    int  total = 0;

    _rx_flush();
    _tx("AT+COPS?\r\n", false);
    _debug("[LTE] >> AT+COPS?\r\n");

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;
            if (strstr(buf, "OK")) goto cops_done;
        }
        osDelay(1);
    }
cops_done:;
    char *s = strchr(buf, '"');
    char *e = s ? strchr(s + 1, '"') : NULL;
    if (s && e)
    {
        osMutexAcquire(g_mutex, osWaitForever);
        int len = e - s - 1;
        if (len > (int)sizeof(g_status.operator_name) - 1)
            len = sizeof(g_status.operator_name) - 1;
        strncpy(g_status.operator_name, s + 1, len);
        g_status.operator_name[len] = '\0';
        osMutexRelease(g_mutex);

        char dbg[64];
        snprintf(dbg, sizeof(dbg), "[LTE] Op: %s\r\n",
                 g_status.operator_name);
        _debug(dbg);
    }
}

static void read_network_time(void)
{
    _sendWait("AT+CTZU=1", "OK", 3000, true);

    char buf[LTE_UART_BUF_SIZE] = {0};
    int  total = 0;

    _rx_flush();
    _tx("AT+QLTS=2\r\n", false);
    _debug("[LTE] >> AT+QLTS=2\r\n");

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;
            if (strstr(buf, "OK")) goto qlts_done;
        }
        osDelay(1);
    }
qlts_done:;
    char *s = strchr(buf, '"');
    char *e = s ? strchr(s + 1, '"') : NULL;
    if (s && e)
    {
        osMutexAcquire(g_mutex, osWaitForever);
        int len = e - s - 1;
        if (len > (int)sizeof(g_status.datetime) - 1)
            len = sizeof(g_status.datetime) - 1;
        strncpy(g_status.datetime, s + 1, len);
        g_status.datetime[len] = '\0';
        g_status.time_synced   = true;
        osMutexRelease(g_mutex);

        char dbg[64];
        snprintf(dbg, sizeof(dbg), "[LTE] Time: %s\r\n",
                 g_status.datetime);
        _debug(dbg);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * read_band — AT+QNWINFO
 * Response: +QNWINFO: "LTE","50001","LTE BAND 3",1850
 * ═══════════════════════════════════════════════════════════════ */
static void read_band(void)
{
    char     buf[LTE_UART_BUF_SIZE] = {0};
    int      total = 0;
    char     dbg[128];

    _rx_flush();
    _tx("AT+QNWINFO\r\n", false);
    _debug("[LTE] >> AT+QNWINFO\r\n");

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 3000)
    {
        while (_rx_available() > 0)
        {
            uint8_t c = _rx_read();
            if (total < (int)sizeof(buf) - 1)
                buf[total++] = c;
            if (strstr(buf, "OK")) goto band_done;
        }
        osDelay(1);
    }
band_done:;
    /* Debug raw response */
    snprintf(dbg, sizeof(dbg), "[LTE] QNWINFO: %.100s\r\n", buf);
    _debug(dbg);

    /* Parse: +QNWINFO: "LTE","50001","LTE BAND 3",1850
     * Field 1: tech  → "LTE"
     * Field 2: mcc   → "50001"
     * Field 3: band  → "LTE BAND 3"  ← yang kita mau
     */
    char *p = strstr(buf, "+QNWINFO:");
    if (p)
    {
        char *q = p;
        /* Skip 2 pasang kutip pertama */
        for (int i = 0; i < 2; i++)
        {
            q = strchr(q, '"');
            if (!q) break;
            q++;
            q = strchr(q, '"');
            if (!q) break;
            q++;
        }
        /* q sekarang setelah kutip penutup field ke-2 */
        if (q)
        {
            char *s = strchr(q, '"');       /* buka kutip field 3 */
            char *e = s ? strchr(s + 1, '"') : NULL; /* tutup kutip field 3 */
            if (s && e)
            {
                osMutexAcquire(g_mutex, osWaitForever);
                int len = e - s - 1;
                if (len > (int)sizeof(g_status.band) - 1)
                    len = sizeof(g_status.band) - 1;
                strncpy(g_status.band, s + 1, len);
                g_status.band[len] = '\0';
                osMutexRelease(g_mutex);

                snprintf(dbg, sizeof(dbg), "[LTE] Band: %s\r\n",
                         g_status.band);
                _debug(dbg);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 * build_json
 * ═══════════════════════════════════════════════════════════════ */
static int build_json(char *buf, size_t buf_len,
                      const sensor_data_t *sensor,
                      const lte_status_t  *lte)
{
    return snprintf(buf, buf_len,
        "{"
        "\"device\":\"%s\","
        "\"ts\":\"%s\","
        "\"environment\":{"
            "\"temp\":%.1f,"
            "\"humidity\":%.1f"
        "},"
        "\"lte\":{"
            "\"rssi\":%d,"
            "\"rsrp\":%d,"
            "\"sinr\":%d,"
            "\"op\":\"%s\","
            "\"band\":\"%s\""
        "}"
        "}",
        MQTT_CLIENT_ID,
        lte->time_synced ? lte->datetime : "--",
        sensor->valid ? sensor->temperature : 0.0f,
        sensor->valid ? sensor->humidity    : 0.0f,
        lte->rssi, lte->rsrp, lte->sinr,
        lte->operator_name, lte->band
    );
}

/* ═══════════════════════════════════════════════════════════════
 * device_init — ATE0 + module ready + signal + operator
 * ═══════════════════════════════════════════════════════════════ */
static bool device_init(void)
{
    _sendWait("ATE0", "OK", 3000, true);
    if (!_moduleReady()) return false;
    read_signal_quality();
    read_operator();
    read_band();
    _debug("[LTE] Device OK\r\n");
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * mqtt_connect — QICSGP → delay → QMTCFG → QMTOPEN → QMTCONN
 * ═══════════════════════════════════════════════════════════════ */
static bool mqtt_connect(void)
{
    char cmd[256];

    /* Reset MQTT state — close jika masih ada session sebelumnya */
    _sendWait("AT+QMTCLOSE=0", "OK", 3000, true);  /* tidak perlu cek return */
    _sendWait("AT+QMTDISC=0",  "+QMTDISC: 0,0", 3000, true);
    osDelay(500);

    /* QICSGP */
    snprintf(cmd, sizeof(cmd),
             "AT+QICSGP=1,1,\"%s\",\"\",\"\",1", LTE_APN);
    _sendWait(cmd, "OK", 5000, true);

    osDelay(2000);

    /* QMTCFG */
    if (!_sendWait("AT+QMTCFG=\"recv/mode\",0,0,1", "OK", 5000, true))
    {
        _debug("[LTE] QMTCFG failed\r\n");
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+QMTOPEN=0,\"%s\",%s", MQTT_HOST, MQTT_PORT);
    if (!_sendWait(cmd, "+QMTOPEN: 0,0", 15000, true))
    {
        _debug("[LTE] Open failed\r\n");
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+QMTCONN=0,\"%s\"", MQTT_CLIENT_ID);
    if (!_sendWait(cmd, "+QMTCONN: 0,0,0", 10000, true))
    {
        _debug("[LTE] Connect failed\r\n");
        return false;
    }

    _debug("[LTE] MQTT connected\r\n");
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * mqtt_publish — QMTPUBEX → "> " → payload → +QMTPUBEX: 0,0,0
 * ═══════════════════════════════════════════════════════════════ */
static bool mqtt_publish(const char *json, int json_len)
{
    char cmd[128];
    (void)json_len;

    snprintf(cmd, sizeof(cmd),
             "AT+QMTPUBEX=0,0,0,0,\"%s\",%d",
             MQTT_PUB_TOPIC, (int)strlen(json));

    if (!_sendWait(cmd, ">", 10000, true))
    {
        _debug("[LTE] No prompt\r\n");
        return false;
    }

    _debug("[LTE] Got prompt\r\n");

    if (!_sendWait(json, "+QMTPUBEX: 0,0,0", 10000, false))
    {
        _debug("[LTE] Publish confirm failed\r\n");
        return false;
    }

    _debug("[LTE] Published OK\r\n");
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * mqtt_disconnect
 * ═══════════════════════════════════════════════════════════════ */
static void mqtt_disconnect(void)
{
    _sendWait("AT+QMTDISC=0", "+QMTDISC: 0,0", 10000, true);
    _sendWait("AT+QIDEACT=1", "OK",             10000, true);
    _debug("[LTE] Disconnected\r\n");
}

/* ═══════════════════════════════════════════════════════════════
 * lte_task
 * ═══════════════════════════════════════════════════════════════ */
static void lte_task(void *arg)
{
    bool dev_ready   = false;
    bool connected   = false;
    int  status_tick = 0;

    char json[512];

    _debug("[LTE] Task started\r\n");
    osDelay(5000);

    while (1)
    {
        if (!dev_ready)
        {
            _debug("[LTE] Device init...\r\n");
            if (!device_init())
            {
                osDelay(10000);
                continue;
            }
            dev_ready = true;
        }

        if (!connected)
        {
            _debug("[LTE] MQTT connecting...\r\n");
            if (!mqtt_connect())
            {
                osDelay(15000);
                continue;
            }
            read_network_time();
            connected = true;
        }

        sensor_data_t sensor;
        sensor_get_last(&sensor);

        lte_status_t lte;
        lte_4g_get_status(&lte);

        int json_len = build_json(json, sizeof(json), &sensor, &lte);

        if (!mqtt_publish(json, json_len))
        {
            mqtt_disconnect();
            connected = false;
            continue;
        }

        status_tick++;
        if (status_tick >= (60000 / MQTT_PUBLISH_INTERVAL_MS))
        {
            read_signal_quality();
            read_network_time();
            status_tick = 0;
        }

        osDelay(MQTT_PUBLISH_INTERVAL_MS);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════ */
void lte_4g_init(void)
{
    g_mutex = osMutexNew(NULL);
    memset(&g_status, 0, sizeof(g_status));
    strncpy(g_status.operator_name, "--", sizeof(g_status.operator_name));
    strncpy(g_status.band,          "--", sizeof(g_status.band));
    strncpy(g_status.datetime,      "--", sizeof(g_status.datetime));

    /* Start UART RX interrupt */
    _rx_start();

    osThreadNew(lte_task, NULL, &lte_task_attr);
    _debug("[LTE] Init done\r\n");
}

void lte_4g_get_status(lte_status_t *out)
{
    osMutexAcquire(g_mutex, osWaitForever);
    memcpy(out, &g_status, sizeof(lte_status_t));
    osMutexRelease(g_mutex);
}
