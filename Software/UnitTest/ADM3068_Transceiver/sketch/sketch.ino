/*
 * Arduino UNO Q SBC
 * External UART Loopback Test
 *
 * USB Serial Monitor : 115200
 * External UART      : 115200
 */

#define DEBUG_BAUD 115200
#define UART_BAUD  115200

uint32_t txPackets = 0;
uint32_t rxPackets = 0;

char rxBuffer[128];
uint8_t rxIndex = 0;

unsigned long previousMillis = 0;

void setup()
{
    Serial.begin(DEBUG_BAUD);

    while (!Serial);

    Serial1.begin(UART_BAUD);

    Serial.println();
    Serial.println("=====================================");
    Serial.println("UNO Q ADM3068E Transceiver RS485 Loopback Test");
    Serial.println("Connect D1(TX) ---> D0(RX)");
    Serial.println("=====================================");
}

void loop()
{
    //-------------------------------
    // Transmit every 100 ms
    //-------------------------------
    if (millis() - previousMillis >= 1000)
    {
        previousMillis = millis();

        Serial1.print("ADM3068_Packet ");
        Serial1.println(txPackets);

        Serial.print("TX -> ADM3068_Packet ");
        Serial.println(txPackets);

        txPackets++;
    }

    //-------------------------------
    // Receive simultaneously
    //-------------------------------
    while (Serial1.available())
    {
        char c = Serial1.read();

        if (c == '\n')
        {
            rxBuffer[rxIndex] = '\0';

            Serial.print("RX <- ");
            Serial.println(rxBuffer);

            rxPackets++;
            rxIndex = 0;
        }
        else if (c != '\r')
        {
            if (rxIndex < sizeof(rxBuffer) - 1)
            {
                rxBuffer[rxIndex++] = c;
            }
        }
    }

    //-------------------------------
    // Statistics every 5 seconds
    //-------------------------------
    static unsigned long statsTimer = 0;

    if (millis() - statsTimer >= 5000)
    {
        statsTimer = millis();

        Serial.println("-------------------------------------");
        Serial.print("Packets Sent     : ");
        Serial.println(txPackets);

        Serial.print("Packets Received : ");
        Serial.println(rxPackets);
        Serial.println("-------------------------------------");
    }
}