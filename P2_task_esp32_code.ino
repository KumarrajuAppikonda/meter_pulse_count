// ============================================================
// ESP32 DEVKIT V1
// BLE + SERIAL MONITOR + PULSE METER RECEIVER
//
// GPIO27 = PULSE INPUT
//
// SG/LD BOARD GPIO27
//        |
//        | PULSE
//        v
// ESP32 DEVKIT GPIO27
//
// BLE DEVICE NAME:
// ESP32_PULSE_METER
//
// COMMANDS:
// 10       -> Set initial reading
// READY    -> Start pulse monitoring
// STOP     -> Stop monitoring
// RESET    -> Reset pulse count
// STATUS   -> Show status
//
// Example:
//
// 10
// READY
//
// Pulse 1 -> Reading 11
// Pulse 2 -> Reading 12
// Pulse 3 -> Reading 13
// ============================================================

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ============================================================
// GPIO
// ============================================================

#define PULSE_INPUT_PIN 27


// ============================================================
// BLE DEVICE NAME
// ============================================================

#define BLE_DEVICE_NAME "ESP32_PULSE_METER"


// ============================================================
// NORDIC UART SERVICE UUID
// ============================================================

#define SERVICE_UUID \
"6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

#define RX_CHARACTERISTIC_UUID \
"6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

#define TX_CHARACTERISTIC_UUID \
"6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


// ============================================================
// BLE OBJECTS
// ============================================================

BLEServer *pServer = nullptr;

BLECharacteristic *pTxCharacteristic = nullptr;

BLECharacteristic *pRxCharacteristic = nullptr;

bool deviceConnected = false;


// ============================================================
// METER VARIABLES
// ============================================================

volatile uint32_t pulseCount = 0;

volatile unsigned long lastPulseTime = 0;

uint32_t initialReading = 0;

uint32_t currentReading = 0;

bool monitoring = false;


// ============================================================
// PULSE FILTER
// ============================================================

const unsigned long MIN_PULSE_INTERVAL_MS = 50;


// ============================================================
// FUNCTION PROTOTYPES
//
// IMPORTANT:
// These declarations solve the compilation errors:
// 'sendBLE' was not declared
// 'processCommand' was not declared
// ============================================================

void sendBLE(String message);

void sendMessage(String message);

void processCommand(String command);

void generateStatus();


// ============================================================
// PULSE INTERRUPT
// ============================================================

void IRAM_ATTR pulseISR()
{
    unsigned long now = millis();

    if ((now - lastPulseTime) >= MIN_PULSE_INTERVAL_MS)
    {
        pulseCount++;

        lastPulseTime = now;
    }
}


// ============================================================
// BLE SERVER CALLBACKS
// ============================================================

class ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server)
    {
        deviceConnected = true;

        Serial.println();
        Serial.println("================================");
        Serial.println("BLE CONNECTED");
        Serial.println("================================");
        Serial.println();

        // Send only after BLE connection exists
        sendBLE("BLE CONNECTED");
    }


    void onDisconnect(BLEServer *server)
    {
        deviceConnected = false;

        Serial.println();
        Serial.println("BLE DISCONNECTED");
        Serial.println();

        // Restart advertising
        BLEDevice::startAdvertising();

        Serial.println("BLE advertising restarted.");
    }
};


// ============================================================
// BLE RX CALLBACK
//
// PHONE -> ESP32
// ============================================================

class RXCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic)
    {
        String command = characteristic->getValue().c_str();

        command.trim();

        command.toUpperCase();


        if (command.length() > 0)
        {
            Serial.print("BLE COMMAND: ");
            Serial.println(command);

            processCommand(command);
        }
    }
};


// ============================================================
// SEND BLE MESSAGE
//
// ESP32 -> PHONE
// ============================================================

void sendBLE(String message)
{
    if (deviceConnected && pTxCharacteristic != nullptr)
    {
        pTxCharacteristic->setValue(message.c_str());

        pTxCharacteristic->notify();
    }
}


// ============================================================
// SEND TO SERIAL + BLE
// ============================================================

void sendMessage(String message)
{
    Serial.println(message);

    sendBLE(message);
}


// ============================================================
// PROCESS COMMAND
// ============================================================

void processCommand(String command)
{
    command.trim();

    command.toUpperCase();


    // ========================================================
    // CHECK WHETHER COMMAND IS A NUMBER
    //
    // Example:
    // 10
    // 25
    // 100
    // ========================================================

    bool numberOnly = true;


    if (command.length() == 0)
    {
        numberOnly = false;
    }


    for (int i = 0; i < command.length(); i++)
    {
        if (!isDigit(command[i]))
        {
            numberOnly = false;

            break;
        }
    }


    // ========================================================
    // INITIAL READING
    // ========================================================

    if (numberOnly)
    {
        uint32_t value = command.toInt();


        noInterrupts();

        initialReading = value;

        pulseCount = 0;

        currentReading = initialReading;

        lastPulseTime = 0;

        interrupts();


        monitoring = false;


        Serial.println();
        Serial.println("--------------------------------");

        Serial.print("Initial Reading = ");
        Serial.println(initialReading);

        Serial.print("Current Reading = ");
        Serial.println(currentReading);

        Serial.println("--------------------------------");

        Serial.println();
        Serial.println("Enter READY.");
        Serial.println();


        sendBLE(
            "Initial Reading = " +
            String(initialReading)
        );

        sendBLE(
            "Current Reading = " +
            String(currentReading)
        );

        sendBLE("Enter READY");


        return;
    }


    // ========================================================
    // READY
    // ========================================================

    if (command == "READY")
    {
        if (initialReading == 0)
        {
            sendMessage(
                "ERROR: Set initial reading first."
            );

            return;
        }


        noInterrupts();

        pulseCount = 0;

        currentReading = initialReading;

        lastPulseTime = millis();

        interrupts();


        monitoring = true;


        sendMessage("================================");
        sendMessage("READY");
        sendMessage(
            "Starting Reading = " +
            String(currentReading)
        );
        sendMessage("Waiting for pulses...");
        sendMessage("================================");


        return;
    }


    // ========================================================
    // STOP
    // ========================================================

    if (command == "STOP")
    {
        monitoring = false;

        sendMessage("PULSE MONITORING STOPPED.");

        return;
    }


    // ========================================================
    // RESET
    // ========================================================

    if (command == "RESET")
    {
        noInterrupts();

        pulseCount = 0;

        currentReading = initialReading;

        interrupts();


        sendMessage("PULSE COUNTER RESET.");

        sendMessage(
            "Current Reading = " +
            String(currentReading)
        );


        return;
    }


    // ========================================================
    // STATUS
    // ========================================================

    if (command == "STATUS")
    {
        generateStatus();

        return;
    }


    // ========================================================
    // UNKNOWN COMMAND
    // ========================================================

    sendMessage("Unknown command.");

    sendMessage("Available commands:");

    sendMessage("10");

    sendMessage("READY");

    sendMessage("STOP");

    sendMessage("RESET");

    sendMessage("STATUS");
}


// ============================================================
// STATUS
// ============================================================

void generateStatus()
{
    uint32_t count;


    noInterrupts();

    count = pulseCount;

    interrupts();


    currentReading =
        initialReading + count;


    sendMessage("------------- STATUS -------------");


    sendMessage(
        "Initial Reading = " +
        String(initialReading)
    );


    sendMessage(
        "Pulse Count = " +
        String(count)
    );


    sendMessage(
        "Current Reading = " +
        String(currentReading)
    );


    if (monitoring)
    {
        sendMessage("Monitoring = RUNNING");
    }
    else
    {
        sendMessage("Monitoring = STOPPED");
    }


    sendMessage("----------------------------------");
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    // ========================================================
    // SERIAL
    // ========================================================

    Serial.begin(115200);

    delay(1000);


    // ========================================================
    // PULSE INPUT
    // ========================================================

    pinMode(PULSE_INPUT_PIN, INPUT);


    attachInterrupt(
        digitalPinToInterrupt(PULSE_INPUT_PIN),
        pulseISR,
        RISING
    );


    // ========================================================
    // BLE INITIALIZATION
    // ========================================================

    BLEDevice::init(BLE_DEVICE_NAME);


    // ========================================================
    // CREATE BLE SERVER
    // ========================================================

    pServer =
        BLEDevice::createServer();

    pServer->setCallbacks(
        new ServerCallbacks()
    );


    // ========================================================
    // CREATE UART SERVICE
    // ========================================================

    BLEService *pService =
        pServer->createService(
            SERVICE_UUID
        );


    // ========================================================
    // TX
    //
    // ESP32 -> PHONE
    //
    // NOTIFY
    // ========================================================

    pTxCharacteristic =
        pService->createCharacteristic(
            TX_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_NOTIFY
        );


    pTxCharacteristic->addDescriptor(
        new BLE2902()
    );


    // ========================================================
    // RX
    //
    // PHONE -> ESP32
    //
    // WRITE
    // ========================================================

    pRxCharacteristic =
        pService->createCharacteristic(
            RX_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
        );


    pRxCharacteristic->setCallbacks(
        new RXCallbacks()
    );


    // ========================================================
    // START SERVICE
    // ========================================================

    pService->start();


    // ========================================================
    // START BLE ADVERTISING
    // ========================================================

    BLEAdvertising *advertising =
        BLEDevice::getAdvertising();


    advertising->addServiceUUID(
        SERVICE_UUID
    );


    advertising->setScanResponse(true);

    advertising->setMinPreferred(0x06);

    advertising->setMinPreferred(0x12);


    BLEDevice::startAdvertising();


    // ========================================================
    // STARTUP MESSAGE
    // ========================================================

    Serial.println();
    Serial.println();
    Serial.println("========================================");
    Serial.println("       ESP32 PULSE METER + BLE");
    Serial.println("========================================");

    Serial.println();

    Serial.print("BLE Name: ");
    Serial.println(BLE_DEVICE_NAME);

    Serial.println();

    Serial.print("Pulse Input: GPIO");
    Serial.println(PULSE_INPUT_PIN);

    Serial.println();

    Serial.println("BLE advertising started.");

    Serial.println();

    Serial.println("Commands:");

    Serial.println("10       -> Set initial reading");

    Serial.println("READY    -> Start monitoring");

    Serial.println("STOP     -> Stop monitoring");

    Serial.println("RESET    -> Reset pulse count");

    Serial.println("STATUS   -> Show status");

    Serial.println();

    Serial.println("Waiting for initial reading...");
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // ========================================================
    // SERIAL MONITOR COMMAND
    // ========================================================

    if (Serial.available())
    {
        String command =
            Serial.readStringUntil('\n');


        command.trim();

        command.toUpperCase();


        if (command.length() > 0)
        {
            Serial.print("SERIAL COMMAND: ");

            Serial.println(command);


            processCommand(command);
        }
    }


    // ========================================================
    // UPDATE READING
    // ========================================================

    if (monitoring)
    {
        uint32_t count;


        noInterrupts();

        count = pulseCount;

        interrupts();


        currentReading =
            initialReading + count;


        // ----------------------------------------------------
        // Detect new pulse
        // ----------------------------------------------------

        static uint32_t previousCount = 0;


        if (count != previousCount)
        {
            previousCount = count;


            String message;


            message =
                "PULSE DETECTED | ";


            message +=
                "Pulse Count = ";


            message +=
                String(count);


            message +=
                " | Reading = ";


            message +=
                String(currentReading);


            // Send to Serial Monitor
            // AND BLE phone

            sendMessage(message);
        }
    }


    delay(5);
}
