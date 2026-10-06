#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ============================================================
// ESP32 GAS METER PULSE COUNTER
// ============================================================
//
// ESP32-WROOM-32
//
// GPIO27 = SENSOR POWER
// GPIO26 = METER READ SENSOR
//
// SENSOR:
//
// Normal state       = HIGH
// Magnet detected    = LOW
//
// HIGH -> LOW        = FALLING EDGE
// FALLING EDGE       = ONE SENSOR EVENT
//
// The sensor event does NOT immediately change the reading.
//
// Sensor detects magnet
//        ↓
// Wait for mechanical dial movement
//        ↓
// Increase pulse count
//        ↓
// Update reading
//
// ============================================================


// ============================================================
// GPIO
// ============================================================

#define SENSOR_POWER_PIN 27
#define PULSE_INPUT_PIN  26


// ============================================================
// BLE DEVICE NAME
// ============================================================

#define BLE_DEVICE_NAME "ESP32_PULSE_METER"


// ============================================================
// BLE UUIDs
// ============================================================

#define SERVICE_UUID \
"6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

#define RX_CHARACTERISTIC_UUID \
"6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

#define TX_CHARACTERISTIC_UUID \
"6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


// ============================================================
// DIAL UPDATE DELAY
// ============================================================
//
// The sensor detects the magnet before the printed dial
// reaches the next number.
//
// Example:
//
// Dial = 78
//     ↓
// Magnet detected
//     ↓
// Reading remains 78
//     ↓
// Wait 1000 ms
//     ↓
// Dial reaches 79
//     ↓
// Reading becomes 79
//
// START WITH 1000 ms.
//
// You can later change this to:
//
// 800
// 1000
// 1200
// 1500
//
// depending on your actual meter.
//
// ============================================================

const unsigned long DIAL_UPDATE_DELAY_MS = 1000;


// ============================================================
// SENSOR RELEASE TIME
// ============================================================
//
// After magnet leaves:
//
// LOW -> HIGH
//
// HIGH must remain stable for 500 ms.
//
// This is only for re-arming the sensor.
//
// It does NOT delay the pulse counting update.
//
// ============================================================

const unsigned long SENSOR_RELEASE_TIME_MS = 500;


// ============================================================
// BLE OBJECTS
// ============================================================

BLEServer *pServer = nullptr;

BLEService *pService = nullptr;

BLECharacteristic *pRxCharacteristic = nullptr;

BLECharacteristic *pTxCharacteristic = nullptr;

volatile bool deviceConnected = false;


// ============================================================
// METER VARIABLES
// ============================================================

// Number of completed/countable pulses
volatile uint32_t pulseCount = 0;


// Initial meter reading entered by user
uint32_t initialReading = 0;


// Current meter reading
uint32_t currentReading = 0;


// ============================================================
// MONITORING
// ============================================================
//
// false = not started
// true  = monitoring
//
// ============================================================

volatile bool monitoring = false;


// ============================================================
// SENSOR ARM STATE
// ============================================================
//
// true:
//     Sensor can detect a new magnet.
//
// false:
//     Sensor is locked until magnet leaves.
//
// ============================================================

volatile bool sensorArmed = false;


// ============================================================
// PULSE EVENT
// ============================================================
//
// ISR sets this TRUE.
//
// Main loop processes it.
//
// ============================================================

volatile bool pulseEvent = false;


// ============================================================
// PENDING PULSE
// ============================================================
//
// Magnet has been detected,
// but the meter reading has NOT been updated yet.
//
// ============================================================

bool pendingPulse = false;

unsigned long pulseDetectedTime = 0;


// ============================================================
// SENSOR HIGH TIMER
// ============================================================

unsigned long sensorHighStartTime = 0;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void processCommand(String command);

void sendBLE(String message);

void sendMessage(String message);

void generateStatus();

void monitorSensor();


// ============================================================
// BLE SERVER CALLBACKS
// ============================================================
//
// Handles BLE connection and disconnection.
//
// ============================================================

class ServerCallbacks : public BLEServerCallbacks
{
    void onConnect(
        BLEServer *server
    ) override
    {
        deviceConnected = true;


        Serial.println();

        Serial.println(
            "=============================="
        );

        Serial.println(
            "BLE CONNECTED"
        );

        Serial.println(
            "=============================="
        );

        Serial.println();
    }


    void onDisconnect(
        BLEServer *server
    ) override
    {
        deviceConnected = false;


        Serial.println();

        Serial.println(
            "BLE DISCONNECTED"
        );

        Serial.println();


        delay(100);


        BLEDevice::startAdvertising();


        Serial.println(
            "BLE advertising restarted."
        );
    }
};


// ============================================================
// BLE RX CALLBACKS
// ============================================================
//
// Phone -> ESP32
//
// Commands:
//
// 69
// 78
// READY
// STOP
// RESET
// STATUS
//
// ============================================================

class RXCallbacks :
    public BLECharacteristicCallbacks
{
    void onWrite(
        BLECharacteristic *characteristic
    ) override
    {
        String command =
            characteristic->getValue();


        command.trim();

        command.toUpperCase();


        if (
            command.length() > 0
        )
        {
            Serial.print(
                "BLE COMMAND: "
            );


            Serial.println(
                command
            );


            processCommand(
                command
            );
        }
    }
};


// ============================================================
// FALLING EDGE INTERRUPT
// ============================================================
//
// GPIO26:
//
// HIGH = normal
// LOW  = magnet
//
// HIGH -> LOW
//      ↓
// FALLING EDGE
//      ↓
// ONE SENSOR EVENT
//
// IMPORTANT:
//
// pulseCount is NOT increased here.
//
// ============================================================

void IRAM_ATTR pulseISR()
{
    // --------------------------------------------------------
    // Do not accept anything before READY
    // --------------------------------------------------------

    if (!monitoring)
    {
        return;
    }


    // --------------------------------------------------------
    // Sensor must be armed
    // --------------------------------------------------------

    if (!sensorArmed)
    {
        return;
    }


    // --------------------------------------------------------
    // Lock sensor immediately
    // --------------------------------------------------------

    sensorArmed = false;


    // --------------------------------------------------------
    // Create event
    // --------------------------------------------------------

    pulseEvent = true;
}


// ============================================================
// SEND BLE
// ============================================================

void sendBLE(
    String message
)
{
    if (
        deviceConnected &&
        pTxCharacteristic != nullptr
    )
    {
        pTxCharacteristic->setValue(
            message.c_str()
        );


        pTxCharacteristic->notify();
    }
}


// ============================================================
// SEND SERIAL + BLE
// ============================================================

void sendMessage(
    String message
)
{
    Serial.println(
        message
    );


    sendBLE(
        message
    );
}


// ============================================================
// PROCESS COMMAND
// ============================================================

void processCommand(
    String command
)
{
    command.trim();

    command.toUpperCase();


    // ========================================================
    // CHECK NUMBER COMMAND
    // ========================================================

    bool numberOnly =
        command.length() > 0;


    for (
        int i = 0;
        i < command.length();
        i++
    )
    {
        if (
            !isDigit(
                command[i]
            )
        )
        {
            numberOnly = false;

            break;
        }
    }


    // ========================================================
    // SET INITIAL READING
    // ========================================================
    //
    // Example:
    //
    // Send:
    //
    // 78
    //
    // Result:
    //
    // Initial Reading = 78
    // Pulse Count = 0
    // Current Reading = 78
    //
    // ========================================================

    if (numberOnly)
    {
        uint32_t value =
            (uint32_t)command.toInt();


        // ----------------------------------------------------
        // Stop monitoring
        // ----------------------------------------------------

        monitoring = false;


        // ----------------------------------------------------
        // Clear interrupt state
        // ----------------------------------------------------

        noInterrupts();

        pulseCount = 0;

        pulseEvent = false;

        sensorArmed = false;

        interrupts();


        // ----------------------------------------------------
        // Clear pending pulse
        // ----------------------------------------------------

        pendingPulse = false;

        pulseDetectedTime = 0;

        sensorHighStartTime = 0;


        // ----------------------------------------------------
        // Set initial reading
        // ----------------------------------------------------

        initialReading =
            value;


        currentReading =
            value;


        // ----------------------------------------------------
        // Display
        // ----------------------------------------------------

        sendMessage(
            "INITIAL READING SET"
        );


        sendMessage(
            "Initial Reading = " +
            String(
                initialReading
            )
        );


        sendMessage(
            "Current Reading = " +
            String(
                currentReading
            )
        );


        sendMessage(
            "Pulse Count = 0"
        );


        sendMessage(
            "Send READY to start."
        );


        return;
    }


    // ========================================================
    // READY
    // ========================================================

    if (
        command == "READY"
    )
    {
        // ----------------------------------------------------
        // Stop temporarily while resetting
        // ----------------------------------------------------

        monitoring = false;


        // ----------------------------------------------------
        // Clear all previous events
        // ----------------------------------------------------

        noInterrupts();

        pulseCount = 0;

        pulseEvent = false;

        sensorArmed = false;

        interrupts();


        pendingPulse = false;

        pulseDetectedTime = 0;

        sensorHighStartTime = 0;


        // ----------------------------------------------------
        // Start monitoring
        // ----------------------------------------------------

        monitoring = true;


        // ----------------------------------------------------
        // Read current sensor
        // ----------------------------------------------------

        int sensorState =
            digitalRead(
                PULSE_INPUT_PIN
            );


        // ----------------------------------------------------
        // SENSOR HIGH
        //
        // No magnet currently present.
        //
        // Arm sensor.
        // ----------------------------------------------------

        if (
            sensorState == HIGH
        )
        {
            noInterrupts();

            sensorArmed = true;

            interrupts();


            Serial.println(
                "SENSOR READY"
            );
        }


        // ----------------------------------------------------
        // SENSOR LOW
        //
        // Magnet is already present.
        //
        // Do NOT count it.
        //
        // Wait until it leaves.
        // ----------------------------------------------------

        else
        {
            noInterrupts();

            sensorArmed = false;

            interrupts();


            Serial.println(
                "SENSOR LOW - WAITING FOR RELEASE"
            );
        }


        // ----------------------------------------------------
        // Start message
        // ----------------------------------------------------

        sendMessage(
            "=============================="
        );


        sendMessage(
            "PULSE MONITORING STARTED"
        );


        sendMessage(
            "Initial Reading = " +
            String(
                initialReading
            )
        );


        sendMessage(
            "Current Reading = " +
            String(
                currentReading
            )
        );


        sendMessage(
            "Pulse Count = 0"
        );


        sendMessage(
            "GPIO27 = SENSOR POWER"
        );


        sendMessage(
            "GPIO26 = SENSOR INPUT"
        );


        sendMessage(
            "NORMAL = HIGH"
        );


        sendMessage(
            "MAGNET = LOW"
        );


        sendMessage(
            "HIGH -> LOW = ONE SENSOR EVENT"
        );


        sendMessage(
            "DIAL UPDATE DELAY = " +
            String(
                DIAL_UPDATE_DELAY_MS
            ) +
            " ms"
        );


        sendMessage(
            "=============================="
        );


        return;
    }


    // ========================================================
    // STOP
    // ========================================================

    if (
        command == "STOP"
    )
    {
        monitoring = false;


        noInterrupts();

        sensorArmed = false;

        pulseEvent = false;

        interrupts();


        pendingPulse = false;

        pulseDetectedTime = 0;

        sensorHighStartTime = 0;


        sendMessage(
            "PULSE MONITORING STOPPED"
        );


        return;
    }


    // ========================================================
    // RESET
    // ========================================================
    //
    // Initial reading remains unchanged.
    //
    // ========================================================

    if (
        command == "RESET"
    )
    {
        monitoring = false;


        noInterrupts();

        pulseCount = 0;

        pulseEvent = false;

        sensorArmed = false;

        interrupts();


        pendingPulse = false;

        pulseDetectedTime = 0;

        sensorHighStartTime = 0;


        currentReading =
            initialReading;


        sendMessage(
            "PULSE COUNTER RESET"
        );


        sendMessage(
            "Initial Reading = " +
            String(
                initialReading
            )
        );


        sendMessage(
            "Current Reading = " +
            String(
                currentReading
            )
        );


        sendMessage(
            "Pulse Count = 0"
        );


        sendMessage(
            "Send READY to start."
        );


        return;
    }


    // ========================================================
    // STATUS
    // ========================================================

    if (
        command == "STATUS"
    )
    {
        generateStatus();

        return;
    }


    // ========================================================
    // UNKNOWN COMMAND
    // ========================================================

    sendMessage(
        "Unknown command."
    );


    sendMessage(
        "Available commands:"
    );


    sendMessage(
        "69 / 78 / 100 = Initial Reading"
    );


    sendMessage(
        "READY = Start Monitoring"
    );


    sendMessage(
        "STOP = Stop Monitoring"
    );


    sendMessage(
        "RESET = Reset Counter"
    );


    sendMessage(
        "STATUS = Status"
    );
}


// ============================================================
// STATUS
// ============================================================

void generateStatus()
{
    uint32_t count;

    bool armed;


    noInterrupts();

    count =
        pulseCount;

    armed =
        sensorArmed;

    interrupts();


    currentReading =
        initialReading +
        count;


    int sensorState =
        digitalRead(
            PULSE_INPUT_PIN
        );


    sendMessage(
        "------------- STATUS -------------"
    );


    sendMessage(
        "Initial Reading = " +
        String(
            initialReading
        )
    );


    sendMessage(
        "Pulse Count = " +
        String(
            count
        )
    );


    sendMessage(
        "Current Reading = " +
        String(
            currentReading
        )
    );


    sendMessage(
        "GPIO26 = " +
        String(
            sensorState == HIGH
            ? "HIGH"
            : "LOW"
        )
    );


    sendMessage(
        "Sensor Armed = " +
        String(
            armed
            ? "YES"
            : "NO"
        )
    );


    sendMessage(
        "Pending Pulse = " +
        String(
            pendingPulse
            ? "YES"
            : "NO"
        )
    );


    sendMessage(
        "Monitoring = " +
        String(
            monitoring
            ? "RUNNING"
            : "STOPPED"
        )
    );


    sendMessage(
        "----------------------------------"
    );
}


// ============================================================
// SENSOR MONITOR
// ============================================================

void monitorSensor()
{
    if (
        !monitoring
    )
    {
        return;
    }


    // ========================================================
    // CHECK NEW SENSOR EVENT
    // ========================================================

    bool newPulse =
        false;


    noInterrupts();

    if (
        pulseEvent
    )
    {
        pulseEvent = false;

        newPulse = true;
    }

    interrupts();


    // ========================================================
    // MAGNET DETECTED
    // ========================================================
    //
    // IMPORTANT:
    //
    // pulseCount is NOT increased here.
    //
    // We only start the timer.
    //
    // ========================================================

    if (
        newPulse
    )
    {
        if (
            !pendingPulse
        )
        {
            pendingPulse = true;


            pulseDetectedTime =
                millis();


            // ------------------------------------------------
            // Reading remains unchanged.
            // ------------------------------------------------

            Serial.println(
                "MAGNET DETECTED - WAITING FOR DIAL"
            );
        }
    }


    // ========================================================
    // WAIT FOR DIAL UPDATE
    // ========================================================

    if (
        pendingPulse
    )
    {
        unsigned long elapsed =
            millis() -
            pulseDetectedTime;


        if (
            elapsed >=
            DIAL_UPDATE_DELAY_MS
        )
        {
            uint32_t count;


            // ------------------------------------------------
            // NOW COUNT
            // ------------------------------------------------

            noInterrupts();

            pulseCount++;

            count =
                pulseCount;

            interrupts();


            // ------------------------------------------------
            // UPDATE READING
            // ------------------------------------------------

            currentReading =
                initialReading +
                count;


            // ------------------------------------------------
            // CLEAR PENDING EVENT
            // ------------------------------------------------

            pendingPulse = false;

            pulseDetectedTime = 0;


            // ------------------------------------------------
            // SERIAL
            // ------------------------------------------------

            Serial.print(
                "PULSE COUNTED | Pulse Count = "
            );


            Serial.print(
                count
            );


            Serial.print(
                " | Reading = "
            );


            Serial.println(
                currentReading
            );


            // ------------------------------------------------
            // BLE
            // ------------------------------------------------

            sendBLE(
                "PULSE COUNTED | Pulse Count = " +
                String(
                    count
                ) +
                " | Reading = " +
                String(
                    currentReading
                )
            );
        }
    }


    // ========================================================
    // SENSOR RELEASE
    // ========================================================
    //
    // After one magnet detection:
    //
    // Sensor is locked.
    //
    // We wait for:
    //
    // LOW -> HIGH
    //
    // HIGH stable for 500 ms.
    //
    // Then sensor is armed again.
    //
    // ========================================================

    if (
        !sensorArmed
    )
    {
        int sensorState =
            digitalRead(
                PULSE_INPUT_PIN
            );


        // ----------------------------------------------------
        // SENSOR STILL LOW
        //
        // Magnet still near sensor.
        //
        // Keep locked.
        // ----------------------------------------------------

        if (
            sensorState == LOW
        )
        {
            sensorHighStartTime = 0;
        }


        // ----------------------------------------------------
        // SENSOR HIGH
        //
        // Magnet moved away.
        // ----------------------------------------------------

        else
        {
            if (
                sensorHighStartTime == 0
            )
            {
                sensorHighStartTime =
                    millis();
            }


            // ------------------------------------------------
            // HIGH STABLE
            // ------------------------------------------------

            if (
                millis() -
                sensorHighStartTime
                >=
                SENSOR_RELEASE_TIME_MS
            )
            {
                sensorArmed =
                    true;


                sensorHighStartTime =
                    0;
            }
        }
    }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    // ========================================================
    // SERIAL
    // ========================================================

    Serial.begin(
        115200
    );


    delay(
        1000
    );


    // ========================================================
    // GPIO27
    //
    // SENSOR POWER
    // ========================================================

    pinMode(
        SENSOR_POWER_PIN,
        OUTPUT
    );


    digitalWrite(
        SENSOR_POWER_PIN,
        HIGH
    );


    // ========================================================
    // GPIO26
    //
    // SENSOR INPUT
    //
    // Meter already has pull-up network.
    //
    // Therefore use INPUT.
    //
    // NOT INPUT_PULLUP.
    //
    // ========================================================

    pinMode(
        PULSE_INPUT_PIN,
        INPUT
    );


    // ========================================================
    // INITIAL SENSOR STATE
    // ========================================================

    monitoring =
        false;


    noInterrupts();

    pulseCount = 0;

    pulseEvent = false;

    sensorArmed = false;

    interrupts();


    pendingPulse =
        false;

    pulseDetectedTime =
        0;

    sensorHighStartTime =
        0;


    // ========================================================
    // FALLING EDGE INTERRUPT
    // ========================================================

    attachInterrupt(
        digitalPinToInterrupt(
            PULSE_INPUT_PIN
        ),
        pulseISR,
        FALLING
    );


    // ========================================================
    // BLE INITIALIZATION
    // ========================================================

    BLEDevice::init(
        BLE_DEVICE_NAME
    );


    // ========================================================
    // CREATE BLE SERVER
    // ========================================================

    pServer =
        BLEDevice::createServer();


    pServer->setCallbacks(
        new ServerCallbacks()
    );


    // ========================================================
    // CREATE BLE SERVICE
    // ========================================================

    pService =
        pServer->createService(
            SERVICE_UUID
        );


    // ========================================================
    // TX CHARACTERISTIC
    //
    // ESP32 -> PHONE
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
    // RX CHARACTERISTIC
    //
    // PHONE -> ESP32
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
    // START BLE SERVICE
    // ========================================================

    pService->start();


    // ========================================================
    // BLE ADVERTISING
    // ========================================================

    BLEAdvertising *advertising =
        BLEDevice::getAdvertising();


    advertising->addServiceUUID(
        SERVICE_UUID
    );


    advertising->setScanResponse(
        true
    );


    advertising->setMinPreferred(
        0x06
    );


    advertising->setMinPreferred(
        0x12
    );


    BLEDevice::startAdvertising();


    // ========================================================
    // STARTUP
    // ========================================================

    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "       ESP32 GAS METER PULSE"
    );

    Serial.println(
        "========================================"
    );

    Serial.println();


    Serial.println(
        "GPIO27 = SENSOR POWER"
    );


    Serial.println(
        "GPIO27 = HIGH"
    );


    Serial.println(
        "GPIO26 = SENSOR INPUT"
    );


    Serial.println(
        "NORMAL = HIGH"
    );


    Serial.println(
        "MAGNET = LOW"
    );


    Serial.println(
        "HIGH -> LOW = FALLING EDGE"
    );


    Serial.println(
        "ONE FALLING EDGE = ONE SENSOR EVENT"
    );


    Serial.println(
        "READING UPDATES AFTER DIAL DELAY"
    );


    Serial.print(
        "DIAL UPDATE DELAY = "
    );


    Serial.print(
        DIAL_UPDATE_DELAY_MS
    );


    Serial.println(
        " ms"
    );


    Serial.println();


    Serial.println(
        "BLE advertising started."
    );


    Serial.println();


    Serial.println(
        "Commands:"
    );


    Serial.println(
        "69 / 78 / 100 = Initial Reading"
    );


    Serial.println(
        "READY = Start Monitoring"
    );


    Serial.println(
        "STOP = Stop Monitoring"
    );


    Serial.println(
        "RESET = Reset Counter"
    );


    Serial.println(
        "STATUS = Show Status"
    );


    Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // ========================================================
    // SERIAL COMMAND
    // ========================================================

    if (
        Serial.available()
    )
    {
        String command =
            Serial.readStringUntil(
                '\n'
            );


        command.trim();

        command.toUpperCase();


        if (
            command.length() > 0
        )
        {
            Serial.print(
                "SERIAL COMMAND: "
            );


            Serial.println(
                command
            );


            processCommand(
                command
            );
        }
    }


    // ========================================================
    // SENSOR MONITOR
    // ========================================================

    if (
        monitoring
    )
    {
        monitorSensor();
    }


    // ========================================================
    // SHORT CPU LOOP DELAY
    // ========================================================
    //
    // This is NOT the meter/dial delay.
    //
    // The actual dial delay is:
    //
    // DIAL_UPDATE_DELAY_MS
    //
    // ========================================================

    delay(1);
}