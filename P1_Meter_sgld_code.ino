// ============================================================
// SG/LD V1 - SIMULATED METER PULSE GENERATOR
//
// GPIO27 = EXISTING GREEN LED + EXTERNAL PULSE OUTPUT
//
// Initial reading:
//     10
//
// READY:
//
//     Pulse 1 -> 11
//     Pulse 2 -> 12
//     Pulse 3 -> 13
//     Pulse 4 -> 14
//
// GPIO27:
//     HIGH = Pulse + Green LED ON
//     LOW  = Pulse OFF + Green LED OFF
// ============================================================

#define PULSE_PIN 27

// ============================================================
// METER VARIABLES
// ============================================================

uint32_t initialReading = 0;
uint32_t currentReading = 0;

bool running = false;

// ============================================================
// PULSE TIMING
// ============================================================

// Time from one pulse to the next
const unsigned long PULSE_INTERVAL = 2000;

// HIGH duration of each pulse
// Increased from 100 ms to 1000 ms
const unsigned long PULSE_WIDTH = 1000;

unsigned long lastPulseTime = 0;

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(500);

    pinMode(PULSE_PIN, OUTPUT);

    digitalWrite(PULSE_PIN, LOW);

    Serial.println();
    Serial.println("========================================");
    Serial.println("       SG/LD METER PULSE GENERATOR");
    Serial.println("========================================");

    Serial.println();

    Serial.println("GPIO27 = GREEN LED + PULSE OUTPUT");

    Serial.println();

    Serial.println("Enter initial reading.");
    Serial.println("Example:");
    Serial.println("10");

    Serial.println();

    Serial.println("Then enter:");
    Serial.println("READY");

    Serial.println();

    Serial.println("Commands:");
    Serial.println("10       -> Set reading");
    Serial.println("READY    -> Start");
    Serial.println("STOP     -> Stop");
    Serial.println("STATUS   -> Status");

    Serial.println();

    Serial.println("Waiting for initial reading...");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    // ========================================================
    // SERIAL INPUT
    // ========================================================

    if (Serial.available())
    {
        String cmd = Serial.readStringUntil('\n');

        cmd.trim();
        cmd.toUpperCase();

        // ====================================================
        // NUMBER CHECK
        // ====================================================

        bool numberOnly = true;

        if (cmd.length() == 0)
        {
            numberOnly = false;
        }

        for (int i = 0; i < cmd.length(); i++)
        {
            if (!isDigit(cmd[i]))
            {
                numberOnly = false;
                break;
            }
        }

        // ====================================================
        // INITIAL READING
        // ====================================================

        if (numberOnly)
        {
            initialReading = cmd.toInt();

            currentReading = initialReading;

            running = false;

            digitalWrite(PULSE_PIN, LOW);

            Serial.println();
            Serial.println("----------------------------------------");

            Serial.print("Initial Reading = ");
            Serial.println(initialReading);

            Serial.print("Current Reading = ");
            Serial.println(currentReading);

            Serial.println("----------------------------------------");

            Serial.println();

            Serial.println("Enter READY");

            Serial.println();
        }

        // ====================================================
        // READY
        // ====================================================

        else if (cmd == "READY")
        {
            if (initialReading == 0)
            {
                Serial.println();
                Serial.println("ERROR!");
                Serial.println("Set initial reading first.");
                Serial.println("Example: 10");
                Serial.println();
            }
            else
            {
                running = true;

                lastPulseTime = millis();

                Serial.println();
                Serial.println("========================================");
                Serial.println("              READY");
                Serial.println("========================================");

                Serial.print("Starting Reading = ");
                Serial.println(currentReading);

                Serial.println();

                Serial.println("Pulse generation started.");

                Serial.println();
            }
        }

        // ====================================================
        // STOP
        // ====================================================

        else if (cmd == "STOP")
        {
            running = false;

            digitalWrite(PULSE_PIN, LOW);

            Serial.println();
            Serial.println("PULSE GENERATION STOPPED");
            Serial.println();
        }

        // ====================================================
        // STATUS
        // ====================================================

        else if (cmd == "STATUS")
        {
            Serial.println();
            Serial.println("------------- STATUS -------------");

            Serial.print("Initial Reading = ");
            Serial.println(initialReading);

            Serial.print("Current Reading = ");
            Serial.println(currentReading);

            Serial.print("Running = ");

            if (running)
            {
                Serial.println("YES");
            }
            else
            {
                Serial.println("NO");
            }

            Serial.println("----------------------------------");
            Serial.println();
        }

        // ====================================================
        // UNKNOWN COMMAND
        // ====================================================

        else
        {
            Serial.println();
            Serial.println("Unknown command.");

            Serial.println("Use:");
            Serial.println("10");
            Serial.println("READY");
            Serial.println("STOP");
            Serial.println("STATUS");

            Serial.println();
        }
    }

    // ========================================================
    // GENERATE PULSE
    // ========================================================

    if (running)
    {
        if (millis() - lastPulseTime >= PULSE_INTERVAL)
        {
            lastPulseTime = millis();

            generatePulse();
        }
    }
}

// ============================================================
// GENERATE PULSE
// ============================================================

void generatePulse()
{
    // --------------------------------------------------------
    // Increase reading
    // --------------------------------------------------------

    currentReading++;

    Serial.println();
    Serial.println("========================================");
    Serial.println("             NEW PULSE");
    Serial.println("========================================");

    Serial.print("Meter Reading = ");
    Serial.println(currentReading);

    Serial.println("GPIO27 = HIGH");
    Serial.println("GREEN LED = ON");

    // ========================================================
    // HIGH
    //
    // Same GPIO27:
    //
    // 1. Green LED ON
    // 2. Pulse signal HIGH
    // 3. Receiver ESP32 detects rising edge
    // ========================================================

    digitalWrite(PULSE_PIN, HIGH);

    delay(PULSE_WIDTH);

    // ========================================================
    // LOW
    // ========================================================

    digitalWrite(PULSE_PIN, LOW);

    Serial.println("GPIO27 = LOW");
    Serial.println("GREEN LED = OFF");

    Serial.println("========================================");
}
