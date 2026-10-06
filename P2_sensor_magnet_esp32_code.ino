#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


// ============================================================
// ESP32 GAS METER SENSOR
// ============================================================
//
// GPIO27 -> Sensor power
// GPIO26 -> Sensor signal
//
// SENSOR LOGIC:
//
// HIGH = No magnet / released
// LOW  = Magnet detected
//
// FALLING EDGE:
//
// HIGH -> LOW
//      |
//      +----> Magnet detected
//      |
//      +----> ONE COUNT
//
// IMPORTANT:
//
// The magnet may remain close to the sensor.
//
// LOW does NOT continuously increase the count.
//
// After one count:
//
//     WAIT FOR LOW -> HIGH
//
// Only after HIGH is stable can the next magnet
// generate another count.
//
// ============================================================


// ============================================================
// GPIO
// ============================================================

#define SENSOR_POWER_PIN 27
#define PULSE_INPUT_PIN  26


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
// SENSOR TIMING
// ============================================================
//
// The sensor signal must remain LOW for this amount of time
// before the magnet is accepted.
//
// This prevents very short noise pulses.
//
// ============================================================

const unsigned long SENSOR_TRIGGER_STABLE_MS = 30;


// ============================================================
// SENSOR RELEASE
// ============================================================
//
// After one magnet has been detected, the sensor is locked.
//
// GPIO26 must return HIGH and remain HIGH for this time.
//
// Then the sensor becomes ready for the next magnet.
//
// ============================================================

const unsigned long SENSOR_RELEASE_STABLE_MS = 300;


// ============================================================
// BLE OBJECTS
// ============================================================

BLEServer *pServer = nullptr;

BLEService *pService = nullptr;

BLECharacteristic *pRxCharacteristic = nullptr;

BLECharacteristic *pTxCharacteristic = nullptr;

bool deviceConnected = false;


// ============================================================
// METER VARIABLES
// ============================================================

uint32_t pulseCount = 0;

uint32_t initialReading = 0;

uint32_t currentReading = 0;


// ============================================================
// MONITORING
// ============================================================

bool monitoring = false;


// ============================================================
// SENSOR STATE MACHINE
// ============================================================
//
// WAITING_FOR_MAGNET:
//
//     Sensor is HIGH.
//     Waiting for magnet.
//     
//
// WAITING_FOR_RELEASE:
//
//     Magnet was detected.
//     One count already happened.
//     Waiting for HIGH.
//
// ============================================================

enum SensorState
{
    WAITING_FOR_MAGNET,
    WAITING_FOR_RELEASE
};


SensorState sensorState =
    WAITING_FOR_MAGNET;


// ============================================================
// SENSOR DEBOUNCE VARIABLES
// ============================================================

int rawSensorState = HIGH;

int stableSensorState = HIGH;

unsigned long stateChangeTime = 0;


// ============================================================
// INTERRUPT FLAG
// ============================================================
//
// The falling-edge interrupt sets this flag.
//
// The actual count is handled safely in loop().
//
// ============================================================

volatile bool fallingEdgeDetected = false;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void processCommand(String command);

void sendBLE(String message);

void sendMessage(String message);

void generateStatus();

void monitorSensor();

void resetSensorState();


// ============================================================
// SENSOR INTERRUPT
// ============================================================
//
// GPIO26:
//
// Normal:
//     HIGH
//
// Magnet:
//     LOW
//
// Therefore:
//
// HIGH -> LOW
//      |
//      +---- FALLING EDGE
//
// ============================================================

void IRAM_ATTR sensorInterrupt()
{
    // --------------------------------------------------------
    // Do not process interrupts when monitoring is OFF.
    // --------------------------------------------------------

    if (!monitoring)
    {
        return;
    }


    // --------------------------------------------------------
    // Only accept interrupt when waiting for magnet.
    //
    // If already waiting for release, ignore it.
    //
    // This is the important protection against
    // continuous counting.
    // --------------------------------------------------------

    if (
        sensorState ==
        WAITING_FOR_MAGNET
    )
    {
        fallingEdgeDetected = true;
    }
}


// ============================================================
// BLE SERVER CALLBACKS
// ============================================================

class ServerCallbacks :
    public BLEServerCallbacks
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


        delay(100);


        BLEDevice::startAdvertising();


        Serial.println(
            "BLE advertising restarted."
        );
    }
};


// ============================================================
// BLE RX CALLBACK
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
// RESET SENSOR STATE
// ============================================================
//
// Read the actual sensor state.
//
// If LOW when starting:
//     Do NOT count.
//
// Wait until the magnet leaves.
//
// ============================================================

void resetSensorState()
{
    rawSensorState =
        digitalRead(
            PULSE_INPUT_PIN
        );


    stableSensorState =
        rawSensorState;


    stateChangeTime =
        millis();


    fallingEdgeDetected =
        false;


    // --------------------------------------------------------
    // Sensor currently LOW
    //
    // Magnet is already near the sensor.
    //
    // Do NOT count it.
    // --------------------------------------------------------

    if (
        stableSensorState == LOW
    )
    {
        sensorState =
            WAITING_FOR_RELEASE;
    }


    // --------------------------------------------------------
    // Sensor currently HIGH
    //
    // Ready for magnet.
    // --------------------------------------------------------

    else
    {
        sensorState =
            WAITING_FOR_MAGNET;
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
    // 69
    //
    // Result:
    //
    // Initial Reading = 69
    // Pulse Count = 0
    // Current Reading = 69
    //
    // ========================================================

    if (
        numberOnly
    )
    {
        initialReading =
            (uint32_t)command.toInt();


        pulseCount =
            0;


        currentReading =
            initialReading;


        monitoring =
            false;


        resetSensorState();


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
        pulseCount =
            0;


        currentReading =
            initialReading;


        resetSensorState();


        monitoring =
            true;


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
            "HIGH = RELEASED"
        );


        sendMessage(
            "LOW = MAGNET"
        );


        sendMessage(
            "FALLING EDGE = MAGNET TRIGGER"
        );


        sendMessage(
            "ONE MAGNET = ONE COUNT"
        );


        // ----------------------------------------------------
        // Tell current sensor condition
        // ----------------------------------------------------

        if (
            digitalRead(
                PULSE_INPUT_PIN
            ) == LOW
        )
        {
            sendMessage(
                "Sensor active - waiting for release"
            );
        }
        else
        {
            sendMessage(
                "Sensor ready"
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
        monitoring =
            false;


        fallingEdgeDetected =
            false;


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
        pulseCount =
            0;


        currentReading =
            initialReading;


        resetSensorState();


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
    // UNKNOWN COMMAND
    // ========================================================

    sendMessage(
        "Unknown command."
    );


    sendMessage(
        "Commands:"
    );


    sendMessage(
        "25 / 69 / 78 = Initial Reading"
    );


    sendMessage(
        "READY = Start"
    );


    sendMessage(
        "STOP = Stop"
    );


    sendMessage(
        "RESET = Reset"
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
    int sensor =
        digitalRead(
            PULSE_INPUT_PIN
        );


    currentReading =
        initialReading +
        pulseCount;


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
            pulseCount
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
        WAITING_FOR_MAGNET
    )
    {
        sendMessage(
            "Sensor State = WAITING FOR MAGNET"
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
// This is the main sensor logic.
//
// IMPORTANT:
//
// We do NOT count continuously while GPIO26 is LOW.
//
// Sequence:
//
// HIGH
//   |
//   | magnet comes close
//   ↓
// LOW
//   |
//   +---- FALLING INTERRUPT
//   |
//   +---- wait until LOW is stable
//   |
//   +---- COUNT = +1
//   |
//   ↓
// WAITING_FOR_RELEASE
//   |
//   | magnet stays close
//   |
//   | NO MORE COUNTS
//   |
//   | magnet moves away
//   ↓
// HIGH
//   |
//   +---- wait until HIGH is stable
//   |
//   ↓
// WAITING_FOR_MAGNET
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


    unsigned long now =
        millis();


    // ========================================================
    // CHECK FALLING EDGE INTERRUPT
    // ========================================================

    bool newFallingEdge =
        false;


    noInterrupts();

    if (
        fallingEdgeDetected
    )
    {
        fallingEdgeDetected =
            false;

        newFallingEdge =
            true;
    }

    interrupts();


    // ========================================================
    // FALLING EDGE DETECTED
    // ========================================================
    //
    // We don't count immediately.
    //
    // We start checking for a stable LOW.
    //
    // ========================================================

    if (
        newFallingEdge
    )
    {
        stateChangeTime =
            now;
    }


    // ========================================================
    // READ SENSOR
    // ========================================================

    int currentRaw =
        digitalRead(
            PULSE_INPUT_PIN
        );


    // ========================================================
    // RAW SENSOR CHANGED
    // ========================================================

    if (
        currentRaw !=
        rawSensorState
    )
    {
        rawSensorState =
            currentRaw;


        stateChangeTime =
            now;
    }


    // ========================================================
    // WAIT UNTIL SENSOR SIGNAL IS STABLE
    // ========================================================

    if (
        (
            now -
            stateChangeTime
        )
        <
        SENSOR_TRIGGER_STABLE_MS
    )
    {
        return;
    }


    // ========================================================
    // UPDATE STABLE SENSOR STATE
    // ========================================================

    if (
        stableSensorState !=
        rawSensorState
    )
    {
        stableSensorState =
            rawSensorState;
    }


    // ========================================================
    // STATE 1
    //
    // WAITING FOR MAGNET
    //
    // Waiting for stable LOW.
    //
    // ========================================================

    if (
        sensorState ==
        WAITING_FOR_MAGNET
    )
    {
        if (
            stableSensorState ==
            LOW
        )
        {
            // ------------------------------------------------
            // REAL MAGNET DETECTED
            // ------------------------------------------------
            //
            // One magnet = one count.
            //
            // ------------------------------------------------

            pulseCount++;


            currentReading =
                initialReading +
                pulseCount;


            // ------------------------------------------------
            // LOCK SENSOR
            //
            // This is the important part.
            //
            // Magnet can stay LOW.
            //
            // It will NOT count again.
            // ------------------------------------------------

            sensorState =
                WAITING_FOR_RELEASE;


            // ------------------------------------------------
            // OUTPUT
            // ------------------------------------------------

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


            // ------------------------------------------------
            // BLE
            // ------------------------------------------------

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


    // ========================================================
    // STATE 2
    //
    // WAITING FOR RELEASE
    //
    // Magnet is still close.
    //
    // DO NOT COUNT.
    //
    // Only stable HIGH allows another pulse.
    //
    // ========================================================

    else if (
        sensorState ==
        WAITING_FOR_RELEASE
    )
    {
        if (
            stableSensorState ==
            HIGH
        )
        {
            // ------------------------------------------------
            // Magnet has left sensor.
            //
            // Sensor is ready for the next rotation.
            // ------------------------------------------------

            sensorState =
                WAITING_FOR_MAGNET;
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


    delay(1000);


    // ========================================================
    // SENSOR POWER
    // ========================================================
    //
    // GPIO27 continuously HIGH.
    //
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
    // SENSOR INPUT
    // ========================================================
    //
    // Your meter sensor has a pull-up network.
    //
    // Therefore GPIO26 normally reads HIGH.
    //
    // INPUT_PULLUP is also enabled here to make the input
    // stable and prevent floating.
    //
    // ========================================================

    pinMode(
        PULSE_INPUT_PIN,
        INPUT_PULLUP
    );


    // ========================================================
    // INITIAL SENSOR STATE
    // ========================================================

    resetSensorState();


    // ========================================================
    // FALLING EDGE INTERRUPT
    // ========================================================
    //
    // HIGH -> LOW
    //
    // Magnet detected.
    //
    // ========================================================

    attachInterrupt(
        digitalPinToInterrupt(
            PULSE_INPUT_PIN
        ),
        sensorInterrupt,
        FALLING
    );


    // ========================================================
    // BLE INIT
    // ========================================================

    BLEDevice::init(
        BLE_DEVICE_NAME
    );


    // ========================================================
    // BLE SERVER
    // ========================================================

    pServer =
        BLEDevice::createServer();


    pServer->setCallbacks(
        new ServerCallbacks()
    );


    // ========================================================
    // BLE SERVICE
    // ========================================================

    pService =
        pServer->createService(
            SERVICE_UUID
        );


    // ========================================================
    // TX
    //
    // ESP32 -> Phone
    //
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
    // Phone -> ESP32
    //
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
        "======================================"
    );


    Serial.println(
        "     ESP32 GAS METER PULSE"
    );


    Serial.println(
        "======================================"
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
        "INPUT_PULLUP = ENABLED"
    );


    Serial.println();


    Serial.println(
        "HIGH = SENSOR RELEASED"
    );


    Serial.println(
        "LOW = MAGNET DETECTED"
    );


    Serial.println();


    Serial.println(
        "FALLING EDGE = MAGNET TRIGGER"
    );


    Serial.println(
        "ONE MAGNET DETECTION = ONE COUNT"
    );


    Serial.println(
        "MAGNET CAN REMAIN LOW"
    );


    Serial.println(
        "NO CONTINUOUS COUNTING"
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
        "25 / 69 / 78 = Initial Reading"
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

    monitorSensor();


    // ========================================================
    // VERY SHORT LOOP DELAY
    // ========================================================

    delay(2);
}