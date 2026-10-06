#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ============================================================
// ESP32 GAS METER PULSE MONITOR
// ============================================================
//
// SENSOR CONNECTION
//
// Sensor wire 1 -> GPIO26
// Sensor wire 2 -> GND
// GPIO26 uses INPUT_PULLUP.
//
// HIGH = sensor released / magnet away
// LOW  = magnet detected
//
// ONE VALID PULSE:
//
//
// ============================================================


// ============================================================
// SENSOR
// ============================================================

#define SENSOR_PIN 26




const uint32_t SENSOR_STABLE_MS = 8;


// ============================================================
// BLE
// ============================================================

#define BLE_DEVICE_NAME "ESP32_PULSE_METER"

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

BLEService *pService = nullptr;

BLECharacteristic *pRxCharacteristic = nullptr;

BLECharacteristic *pTxCharacteristic = nullptr;

bool deviceConnected = false;


// ============================================================
// METER
// ============================================================

volatile uint32_t pulseCount = 0;

uint32_t initialReading = 0;

uint32_t currentReading = 0;


// ============================================================
// MONITORING
// ============================================================

bool monitoring = false;


// ============================================================
// SENSOR STATES
// ============================================================

enum SensorState
{
    SENSOR_READY_FOR_MAGNET,

    SENSOR_MAGNET_DETECTED,

    SENSOR_WAITING_FOR_RELEASE
};


SensorState sensorState =
    SENSOR_READY_FOR_MAGNET;


// ============================================================
// SENSOR FILTER VARIABLES
// ============================================================

int lastRawState = HIGH;

int stableState = HIGH;

uint32_t stateChangeTime = 0;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void processCommand(String command);

void sendBLE(String message);

void sendMessage(String message);

void generateStatus();

void resetSensorFilter();

void monitorSensor();


// ============================================================
// BLE SERVER CALLBACKS
// ============================================================

class ServerCallbacks : public BLEServerCallbacks
{
public:

    void onConnect(
        BLEServer *server
    ) override
    {
        deviceConnected = true;

        Serial.println();
        Serial.println(
            "BLE CONNECTED"
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
    }
};


// ============================================================
// BLE RX CALLBACKS
// ============================================================

class RXCallbacks :
    public BLECharacteristicCallbacks
{
public:

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
                "COMMAND: "
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
// BLE SEND
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
// SERIAL + BLE
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
// RESET SENSOR FILTER
// ============================================================
//
// Reads the actual sensor state.
//
// If magnet is already present when READY is sent,
// it will NOT be counted.
//
// ============================================================

void resetSensorFilter()
{
    int state =
        digitalRead(
            SENSOR_PIN
        );


    lastRawState =
        state;

    stableState =
        state;

    stateChangeTime =
        millis();


    if (
        state == LOW
    )
    {
        sensorState =
            SENSOR_WAITING_FOR_RELEASE;
    }
    else
    {
        sensorState =
            SENSOR_READY_FOR_MAGNET;
    }
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
    // NUMBER = INITIAL READING
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


    if (
        numberOnly
    )
    {
        monitoring = false;


        noInterrupts();

        pulseCount = 0;

        interrupts();


        initialReading =
            (uint32_t)command.toInt();


        currentReading =
            initialReading;


        resetSensorFilter();


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
        monitoring = false;


        noInterrupts();

        pulseCount = 0;

        interrupts();


        currentReading =
            initialReading;


        // ----------------------------------------------------
        // Read the actual sensor condition.
        // ----------------------------------------------------

        resetSensorFilter();


        // ----------------------------------------------------
        // Start monitoring.
        // ----------------------------------------------------

        monitoring = true;


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
            "GPIO26 = SENSOR INPUT"
        );


        sendMessage(
            "SENSOR WIRE 1 = GPIO26"
        );


        sendMessage(
            "SENSOR WIRE 2 = GND"
        );


        sendMessage(
            "NORMAL = HIGH"
        );


        sendMessage(
            "MAGNET = LOW"
        );


        sendMessage(
            "LOW STABLE = VALID MAGNET"
        );


        sendMessage(
            "ONE MAGNET = ONE PULSE"
        );


        sendMessage(
            "NO ADVANCE COUNT"
        );


        sendMessage(
            "NO METER READING DELAY"
        );


        if (
            sensorState ==
            SENSOR_WAITING_FOR_RELEASE
        )
        {
            sendMessage(
                "MAGNET PRESENT - WAITING FOR RELEASE"
            );
        }
        else
        {
            sendMessage(
                "SENSOR READY"
            );
        }


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


        sendMessage(
            "PULSE MONITORING STOPPED"
        );


        return;
    }


    // ========================================================
    // RESET
    // ========================================================

    if (
        command == "RESET"
    )
    {
        monitoring = false;


        noInterrupts();

        pulseCount = 0;

        interrupts();


        currentReading =
            initialReading;


        resetSensorFilter();


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
    // UNKNOWN
    // ========================================================

    sendMessage(
        "Unknown command."
    );
}


// ============================================================
// STATUS
// ============================================================

void generateStatus()
{
    uint32_t count;


    noInterrupts();

    count =
        pulseCount;

    interrupts();


    currentReading =
        initialReading +
        count;


    int sensor =
        digitalRead(
            SENSOR_PIN
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
            sensor == HIGH
            ? "HIGH"
            : "LOW"
        )
    );


    if (
        sensorState ==
        SENSOR_READY_FOR_MAGNET
    )
    {
        sendMessage(
            "Sensor State = READY"
        );
    }
    else if (
        sensorState ==
        SENSOR_MAGNET_DETECTED
    )
    {
        sendMessage(
            "Sensor State = MAGNET DETECTED"
        );
    }
    else
    {
        sendMessage(
            "Sensor State = WAITING FOR RELEASE"
        );
    }


    sendMessage(
        "----------------------------------"
    );
}


// ============================================================
// SENSOR MONITOR
// ============================================================
//
// IMPORTANT:
//
// We do NOT use the raw FALLING interrupt.
//
// We continuously qualify the actual GPIO state.
//
// This prevents:
//
// HIGH
// LOW
// HIGH
// LOW
// HIGH
//
// caused by contact bounce/noise from becoming:
//
// 1
// 2
// 3
//
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
    // READ RAW SENSOR
    // ========================================================

    int rawState =
        digitalRead(
            SENSOR_PIN
        );


    // ========================================================
    // RAW STATE CHANGED
    // ========================================================

    if (
        rawState != lastRawState
    )
    {
        lastRawState =
            rawState;

        stateChangeTime =
            millis();

        return;
    }


    // ========================================================
    // RAW STATE HAS NOT CHANGED
    //
    // Check whether it stayed stable long enough.
    // ========================================================

    if (
        stableState != rawState
    )
    {
        if (
            millis() -
            stateChangeTime
            >=
            SENSOR_STABLE_MS
        )
        {
            // ------------------------------------------------
            // New state is now VALID.
            // ------------------------------------------------

            stableState =
                rawState;


            // =================================================
            // HIGH -> LOW
            // =================================================

            if (
                stableState == LOW
            )
            {
                // --------------------------------------------
                // Only count if we were waiting for a magnet.
                // --------------------------------------------

                if (
                    sensorState ==
                    SENSOR_READY_FOR_MAGNET
                )
                {
                    pulseCount++;


                    currentReading =
                        initialReading +
                        pulseCount;


                    // ----------------------------------------
                    // LOCK.
                    // ----------------------------------------

                    sensorState =
                        SENSOR_MAGNET_DETECTED;


                    // ----------------------------------------
                    // SERIAL
                    // ----------------------------------------

                    Serial.print(
                        "PULSE DETECTED | Pulse Count = "
                    );


                    Serial.print(
                        pulseCount
                    );


                    Serial.print(
                        " | Reading = "
                    );


                    Serial.println(
                        currentReading
                    );


                    // ----------------------------------------
                    // BLE
                    // ----------------------------------------

                    sendBLE(
                        "PULSE DETECTED | Pulse Count = " +
                        String(
                            pulseCount
                        ) +
                        " | Reading = " +
                        String(
                            currentReading
                        )
                    );
                }
            }


            // =================================================
            // LOW -> HIGH
            // =================================================

            else
            {
                // --------------------------------------------
                // Magnet has completely left.
                //
                // Now we can accept the NEXT magnet.
                // --------------------------------------------

                if (
                    sensorState ==
                    SENSOR_MAGNET_DETECTED ||
                    sensorState ==
                    SENSOR_WAITING_FOR_RELEASE
                )
                {
                    sensorState =
                        SENSOR_READY_FOR_MAGNET;
                }
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
    // SENSOR
    // ========================================================
    //
    // GPIO26 -> sensor
    // GND    -> sensor
    //
    // Internal pull-up.
    //
    // ========================================================

    pinMode(
        SENSOR_PIN,
        INPUT_PULLUP
    );


    resetSensorFilter();


    // ========================================================
    // BLE
    // ========================================================

    BLEDevice::init(
        BLE_DEVICE_NAME
    );


    pServer =
        BLEDevice::createServer();


    pServer->setCallbacks(
        new ServerCallbacks()
    );


    pService =
        pServer->createService(
            SERVICE_UUID
        );


    // ========================================================
    // TX
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
    // START BLE
    // ========================================================

    pService->start();


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
        "GPIO26 = SENSOR INPUT"
    );


    Serial.println(
        "GPIO27 = NOT USED"
    );


    Serial.println(
        "SENSOR WIRE 1 = GPIO26"
    );


    Serial.println(
        "SENSOR WIRE 2 = GND"
    );


    Serial.println();


    Serial.println(
        "INPUT_PULLUP = ENABLED"
    );


    Serial.println(
        "NORMAL = HIGH"
    );


    Serial.println(
        "MAGNET = LOW"
    );


    Serial.println(
        "LOW STABLE = VALID PULSE"
    );


    Serial.println(
        "ONE MAGNET = ONE COUNT"
    );


    Serial.println(
        "NO ADVANCE COUNT"
    );


    Serial.println(
        "NO METER READING DELAY"
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
        "25 / 68 / 84 = Initial Reading"
    );


    Serial.println(
        "READY = Start"
    );


    Serial.println(
        "STOP = Stop"
    );


    Serial.println(
        "RESET = Reset Counter"
    );


    Serial.println(
        "STATUS = Status"
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
            processCommand(
                command
            );
        }
    }


    // ========================================================
    // SENSOR
    // ========================================================

    monitorSensor();


    // ========================================================
    // VERY SHORT CPU LOOP DELAY
    // ========================================================
    //
    // This is NOT a meter delay.
    //
    // Sensor is checked continuously.
    //
    // ========================================================

    delay(1);
}