# ESP32 EC600K MQTT Test

Sketch Arduino untuk testing koneksi Quectel EC600K 4G LTE ke MQTT broker menggunakan ESP32. Digunakan sebagai referensi debugging sebelum implementasi di STM32.

---

## Tujuan

Memverifikasi bahwa:
- Modul EC600K terhubung dan respond AT command
- APN dan koneksi jaringan berfungsi
- MQTT broker dapat dijangkau
- Publish data berhasil

---

## Hardware

| Component | Description |
|-----------|-------------|
| MCU | ESP32 (DevKit atau sejenisnya) |
| 4G LTE | Quectel EC600K-CN module |

### Wiring

| EC600K Pin | ESP32 Pin | Note |
|-----------|-----------|------|
| VIN | 5V | Power |
| GND | GND | Common ground |
| TX | GPIO16 (RX2) | EC600K TX → ESP32 RX |
| RX | GPIO17 (TX2) | EC600K RX → ESP32 TX |

---

## Config

Ubah di bagian atas sketch sebelum upload:

```cpp
String APN      = "internet";        /* APN provider SIM card */
String HOST     = "xxx.xxx.xxx.xxx";   /* IP atau domain broker */
String PORT     = "1883";
String DEVNAME  = "ESP32-TEST";      /* device name unik */
String PUBTOPIC = "STM32-001/env";   /* topic publish */
```

---

## Cara Pakai

1. Upload sketch ke ESP32
2. Buka Serial Monitor — **baud rate 115200**
3. Tunggu — sketch otomatis menjalankan 9 test di `setup()`
4. Lihat hasil tiap test di serial monitor

Untuk masuk loop mode (publish terus-menerus):
- Ketik `MQTT` di serial monitor lalu Enter

Untuk disconnect:
- Ketik `DISC` di serial monitor lalu Enter

Untuk publish manual:
- Ketik `PUB#pesan kamu#` lalu Enter

---

## Test Sequence (setup otomatis)

| Step | Command | Expected |
|------|---------|----------|
| Test 1 | `ATE0` | `OK` — echo dimatikan |
| Test 2 | `AT+QINISTAT` | `+QINISTAT: 1` — modul siap |
| Test 3 | `AT+CSQ` | `+CSQ: xx,99` — signal quality |
| Test 4 | `AT+COPS?` | `+COPS: 0,0,"OPERATOR",7` |
| Test 5 | `AT+QICSGP` | `OK` — APN set |
| Test 6 | `AT+QMTCFG` | `OK` — recv mode set |
| Test 7 | `AT+QMTOPEN` | `+QMTOPEN: 0,0` — network open |
| Test 8 | `AT+QMTCONN` | `+QMTCONN: 0,0,0` — connected |
| Test 9 | `AT+QMTPUBEX` | `+QMTPUBEX: 0,0,0` — published |

---

## Expected Serial Output

```
[TEST 1] Disable echo (ATE0)

ATE0
Response:
ATE0
OK

[TEST 2] Check module ready
[TEST] Checking module state...

+QINISTAT: 1

OK
[TEST] Module ready

[TEST 3] Signal quality

AT+CSQ
Response:

+CSQ: 31,99

OK

[TEST 4] Operator

AT+COPS?
Response:

+COPS: 0,0,"INDOSATOOREDOO",7

OK

[TEST 5] Set APN: internet

AT+QICSGP=1,1,"internet","","",1
Response:

OK

[TEST 6] MQTT recv mode

AT+QMTCFG="recv/mode",0,0,1
Response:

OK

[TEST 7] MQTT open 72.62.124.206:1883

AT+QMTOPEN=0,"72.62.124.206",1883
Response:

OK

+QMTOPEN: 0,0

[TEST 8] MQTT connect

AT+QMTCONN=0,"ESP32-TEST"
Response:

OK

+QMTCONN: 0,0,0

[TEST 9] Publish
[TEST] Sending QMTPUBEX...

AT+QMTPUBEX=0,0,0,0,"STM32-001/env",67
Response:

>

[TEST] Sending payload...

{"device":"ESP32-TEST","environment":{"temp":31.5,"humidity":80.8}}
Response:

OK

+QMTPUBEX: 0,0,0

[TEST] Done — check broker for message
[TEST] Type MQTT in serial monitor to enter loop mode
```


---

## Troubleshooting

| Masalah | Kemungkinan Penyebab | Solusi |
|---------|---------------------|--------|
| Test 2 timeout | Modul belum boot | Naikkan `delay(5000)` di setup |
| Test 3 `+CSQ: 99,99` | Tidak ada sinyal | Pindah lokasi, cek antena |
| Test 6 timeout | Modul busy setelah QICSGP | Naikkan `delay(2000)` |
| Test 7 `+QMTOPEN: 0,3` | Broker tidak reachable | Cek IP/port broker |
| Test 9 tidak dapat `>` | MQTT session masalah | Tambah `AT+QMTCLOSE=0` sebelum QICSGP |

---

## Referensi

- [sparkworks.id EC600K MQTT example](https://sparkworks.id) — kode dasar
- [Quectel EC600K AT Commands Manual](https://www.quectel.com) — referensi AT command
