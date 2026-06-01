/* 
 * ESP32 EC600K MQTT Test
 * Adapted from sparkworks.id
 * 
 * Wiring:
 *   EC600K TX → GPIO16 (ESP32 RX2)
 *   EC600K RX → GPIO17 (ESP32 TX2)
 */

String APN      = "internet";
String HOST     = "xxx.xxx.xxx.xxx";
String PORT     = "1883";
String DEVNAME  = "ESP32-TEST";
String USER     = "";
String PASS     = "";
String SUBTOPIC = "ESP32-TEST/cmd";
String PUBTOPIC = "STM32-001/env";

bool MQTT      = false;
bool connected = false;

String sendData(String command, const int timeout)
{
    String response = "";
    Serial2.println(command);
    long int time = millis();
    while ((time + timeout) > millis())
    {
        if (Serial2.available())
            response = Serial2.readString();
    }
    Serial.print(response);
    return response;
}

String sendDataWaitResponse(String command, String waitString,
                             const int timeout, bool ln)
{
    String response = "";
    bool   timeoutReach = true;

    if (ln) {
        Serial.println();
        Serial.println(command);
        Serial2.println(command);
    } else {
        Serial.println();
        Serial.print(command);
        Serial2.print(command);
    }

    long int time = millis();
    while ((time + timeout) > millis())
    {
        if (Serial2.available())
        {
            response = Serial2.readString();
            if (response.indexOf(waitString) >= 0)
            {
                if (!ln) Serial.println();
                Serial.println("Response: ");
                Serial.println(response);
                timeoutReach = false;
                break;
            }
        }
    }

    if (timeoutReach)
        Serial.println("Request Timeout");

    return response;
}

bool moduleStateCheck()
{
    bool moduleState = false;
    Serial.println("[TEST] Checking module state...");
    for (int i = 0; i < 15; i++)
    {
        String msg = sendData("AT+QINISTAT", 1000);
        if (msg.indexOf("+QINISTAT: 1") >= 0)
        {
            Serial.println("[TEST] Module ready");
            moduleState = true;
            break;
        }
        Serial.print("[TEST] Waiting... ");
        Serial.println(i + 1);
        delay(1000);
    }
    return moduleState;
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, 16, 17);

    delay(5000);

    Serial.println("\r\n[TEST 1] Disable echo (ATE0)");
    sendDataWaitResponse("ATE0", "OK", 3000, true);

    Serial.println("\r\n[TEST 2] Check module ready");
    moduleStateCheck();

    Serial.println("\r\n[TEST 3] Signal quality");
    sendDataWaitResponse("AT+CSQ", "+CSQ:", 3000, true);

    Serial.println("\r\n[TEST 4] Operator");
    sendDataWaitResponse("AT+COPS?", "+COPS:", 3000, true);

    Serial.println("\r\n[TEST 5] Set APN: " + APN);
    sendDataWaitResponse(
        "AT+QICSGP=1,1,\"" + APN + "\",\"\",\"\",1",
        "OK", 5000, true);

    delay(2000);

    Serial.println("\r\n[TEST 6] MQTT recv mode");
    sendDataWaitResponse("AT+QMTCFG=\"recv/mode\",0,0,1", "OK", 5000, true);

    Serial.println("\r\n[TEST 7] MQTT open " + HOST + ":" + PORT);
    sendDataWaitResponse(
        "AT+QMTOPEN=0,\"" + HOST + "\"," + PORT,
        "+QMTOPEN: 0,0", 15000, true);

    Serial.println("\r\n[TEST 8] MQTT connect");
    sendDataWaitResponse(
        "AT+QMTCONN=0,\"" + DEVNAME + "\"",
        "+QMTCONN: 0,0,0", 10000, true);

    Serial.println("\r\n[TEST 9] Publish");
    String PUBMESSAGE = "{\"device\":\"ESP32-TEST\",\"environment\":{\"temp\":31.5,\"humidity\":80.8}}";
    unsigned int PUBLENGTH = PUBMESSAGE.length();

    sendDataWaitResponse(
        "AT+QMTPUBEX=0,0,0,0,\"" + PUBTOPIC + "\"," + String(PUBLENGTH),
        ">", 10000, true);

    sendDataWaitResponse(PUBMESSAGE, "+QMTPUBEX: 0,0,0", 10000, false);

    Serial.println("\r\n[TEST] Done — check broker for message");
    Serial.println("[TEST] Type MQTT in serial monitor to enter loop mode");
}

void loop()
{
    if (Serial.available())
    {
        String recv = Serial.readStringUntil('\n');
        Serial.println(recv);
        if (recv.indexOf("MQTT") >= 0)
            MQTT = true;
    }

    while (MQTT)
    {
        if (!connected)
        {
            moduleStateCheck();
            sendDataWaitResponse(
                "AT+QICSGP=1,1,\"" + APN + "\",\"\",\"\",1",
                "OK", 5000, true);
            sendDataWaitResponse(
                "AT+QMTCFG=\"recv/mode\",0,0,1",
                "OK", 5000, true);
            sendDataWaitResponse(
                "AT+QMTOPEN=0,\"" + HOST + "\"," + PORT,
                "+QMTOPEN: 0,0", 10000, true);
            sendDataWaitResponse(
                "AT+QMTCONN=0,\"" + DEVNAME + "\"",
                "+QMTCONN: 0,0,0", 10000, true);

            String PUBMESSAGE = "{\"device\":\"ESP32-TEST\",\"environment\":{\"temp\":31.5,\"humidity\":80.8}}";
            unsigned int PUBLENGTH = PUBMESSAGE.length();

            sendDataWaitResponse(
                "AT+QMTPUBEX=0,0,0,0,\"" + PUBTOPIC + "\"," + String(PUBLENGTH),
                ">", 10000, true);
            sendDataWaitResponse(PUBMESSAGE, "+QMTPUBEX: 0,0,0", 10000, false);

            connected = true;
        }
        else
        {
            if (Serial2.available())
                Serial.write(Serial2.read());

            if (Serial.available())
            {
                String recv = Serial.readStringUntil('\n');
                if (recv.indexOf("DISC") >= 0)
                {
                    sendDataWaitResponse("AT+QMTDISC=0", "+QMTDISC: 0,0", 10000, true);
                    sendDataWaitResponse("AT+QIDEACT=1", "OK", 10000, true);
                    connected = false;
                    MQTT = false;
                    break;
                }
                else if (recv.indexOf("PUB#") >= 0)
                {
                    int sep1 = recv.indexOf("PUB#");
                    int sep2 = recv.indexOf("#", sep1 + 4);
                    String msg = recv.substring(sep1 + 4, sep2);
                    int msgLength = msg.length();

                    sendDataWaitResponse(
                        "AT+QMTPUBEX=0,0,0,0,\"" + PUBTOPIC + "\"," + String(msgLength),
                        ">", 10000, true);
                    sendDataWaitResponse(msg, "+QMTPUBEX: 0,0,0", 10000, false);
                }
            }
        }
    }
}
