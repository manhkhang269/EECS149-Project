//EMG Peak Detection solution

#include <PeakDetection.h>
#include <Wire.h>
#include <SPI.h>
#include "heartRate.h"
#include "MAX30105.h"

#define pin_emg A0
#define pin_acc A3
#define pin_flex A5
#define pin_buzzer 8

MAX30105 particleSensor;

PeakDetection peakDetection_emg;
PeakDetection peakDetection_acc;
PeakDetection peakDetection_flex;

unsigned long stepMillis_emg;
unsigned long stepMillis_acc;
unsigned long stepMillis_flex;
unsigned long startMillis_emg;
unsigned long startMillis_acc;
unsigned long startMillis_flex;
unsigned long currentMillis_emg;
unsigned long currentMillis_acc;
unsigned long currentMillis_flex;

const unsigned long period_emg = 30;
const unsigned long period_acc = 30;
const unsigned long period_flex = 30;

static int prev_peak_high_emg;
static int prev_peak_high_acc;
static int prev_peak_high_flex;
static int current_peak_emg;
static int current_peak_acc;
static int current_peak_flex;
static int peak_high_count_emg = 0;
static int peak_high_count_acc = 0;
static int peak_high_count_flex = 0;

static int same_peak_emg = 0;
static int same_peak_acc = 0;
static int same_peak_flex = 0;

const byte RATE_SIZE_EMG = 4; //Increase this for more averaging. 4 is good.
const byte RATE_SIZE_ACC = 4;
const byte RATE_SIZE_flex = 4;

static int step_breath_counter = 0;
static int step_beeping_count = 0;
bool beepped = false;

byte rateSpot_emg = 0;
byte rateSpot_acc = 0;
byte rateSpot_flex = 0;
long lastStep_emg = 0; //Time at which the last step occurred
long lastStep_acc = 0;
long lastStep_flex = 0;

int flex_raw;
int stepAvg_emg;
int stepAvg_acc;
int stepAvg_flex;
float stepPerMinute_emg;
float stepPerMinute_acc;
float stepPerMinute_flex;

///Hear Rate
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE];    //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

//SPI
volatile uint16_t Slavereceived;
volatile uint8_t SPI_recieved = 0;

typedef struct
{
    uint32_t max_red;
    uint32_t max_ir;
    uint16_t gsr;
    uint16_t flex;
    uint16_t emg1;
    uint16_t emg2;
} data_packet_t;

static data_packet_t data_pkg;

typedef struct
{
    uint8_t hr;
    uint8_t br;
    uint8_t st_emg;
    uint8_t st_acc;
} stat_packet_t;

static stat_packet_t stat_pkg;

void setup()
{
    SPI.begin();
    Serial.begin(115200);
    pinMode(A0, INPUT);
    pinMode(A7, INPUT);
    pinMode(8, OUTPUT);

    peakDetection_emg.begin(30, 3.8, 0.6);
    peakDetection_acc.begin(30, 3.8, 0.6);
    startMillis_emg = millis();
    startMillis_acc = millis();

    //Initialize SPI
    pinMode(MISO, OUTPUT);
    SPCR |= _BV(SPE);      //Enable SPI ISR
    SPI.attachInterrupt(); //Take incomming data from master and store in SPI received

    // Initialize HR
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
    {
        Serial.println("MAX30105 was not found. Please check wiring/power. ");
        while (1)
            ;
    }

    particleSensor.setup();                    //Configure sensor with default settings
    particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
    particleSensor.setPulseAmplitudeGreen(0);  //Turn off Green LED
}

//SPI ISR
ISR(SPI_STC_vect)
{
    //Slavereceived = SPDR;
    //Rules:
    //0x01 - Raw Data; 0x10 - BPM (All senser measurement per minute)
    SPI_recieved = SPDR;
}

void flex()
{

    //Peak Smoothing
    currentMillis_flex = millis();
    if (peak_high_count_flex == 2)
    {
        if (currentMillis_flex - startMillis_flex < 120)
        {
            same_peak_flex = 1;
            peak_high_count_flex = 0;
        }
    }
    else if (currentMillis_flex - startMillis_flex > 120)
    {
        same_peak_flex = 0;
        peak_high_count_flex = 0;
    }

    double data = (double)analogRead(pin_flex) / 64 - 1;
    data_pkg.flex = (uint16_t)data;
    peakDetection_flex.add(data);
    int peak = peakDetection_flex.getPeak();
    if (peak == 1)
    {
        startMillis_flex = currentMillis_flex;
        peak_high_count_flex++;
    }
}

void step_hr()
{
    long irValue = particleSensor.getIR();
    data_pkg.max_ir = irValue;

    if (checkForBeat(irValue) == true)
    {
        //We sensed a beat!
        long delta = millis() - lastBeat;
        lastBeat = millis();

        beatsPerMinute = 60 / (delta / 1000.0);

        if (beatsPerMinute < 255 && beatsPerMinute > 50)
        {
            stat_pkg.hr = (byte)beatsPerMinute;
        }
    }
}

void step_emg()
{

    //Peak Smoothing
    currentMillis_emg = millis();
    if (peak_high_count_emg == 2)
    {
        if (currentMillis_emg - startMillis_emg < 120)
        {
            same_peak_emg = 1;
            peak_high_count_emg = 0;
        }
    }
    else if (currentMillis_emg - startMillis_emg > 120)
    {
        same_peak_emg = 0;
        peak_high_count_emg = 0;
    }

    double data = (double)analogRead(pin_emg) / 64 - 1;
    data_pkg.emg1 = (uint16_t)data;
    peakDetection_emg.add(data);
    int peak = peakDetection_emg.getPeak();
    if (peak == 1)
    {
        startMillis_emg = currentMillis_emg;
        peak_high_count_emg++;
    }

    //Step per minute counting
    if (same_peak_emg)
    {

        long delta = millis() - lastStep_emg;
        lastStep_emg = millis();

        stepPerMinute_emg = 60 / (delta / 1000.0);

        if (stepPerMinute_emg < 250 && stepPerMinute_emg > 20)
        {
            stat_pkg.st_emg = (byte)stepPerMinute_emg;
        }
    }
}

void step_acc()
{

    //Peak Smoothing
    currentMillis_acc = millis();
    if (peak_high_count_acc == 2)
    {
        if (currentMillis_acc - startMillis_acc < 120)
        {
            same_peak_acc = 1;
            peak_high_count_acc = 0;
        }
    }
    else if (currentMillis_acc - startMillis_acc > 120)
    {
        same_peak_acc = 0;
        peak_high_count_acc = 0;
    }

    double data = (double)analogRead(pin_acc) / 64 - 1;
    peakDetection_acc.add(data);
    int peak = peakDetection_acc.getPeak();
    if (peak == 1)
    {
        startMillis_acc = currentMillis_acc;
        peak_high_count_acc++;
    }

    //Step per minute counting
    if (same_peak_acc)
    {

        long delta = millis() - lastStep_acc;
        lastStep_acc = millis();

        stepPerMinute_acc = 60 / (delta / 1000.0);

        if (stepPerMinute_acc < 280 && stepPerMinute_acc > 0)
        {
            stat_pkg.st_acc = (byte)stepPerMinute_acc;
        }
    }
}

void loop()
{
    flex();
    step_hr();
    step_emg();
    step_acc();

    if (same_peak_acc && stat_pkg.st_acc > 150)
    {
        beepped = true;
        digitalWrite(pin_buzzer, HIGH);
        delay(10);
        digitalWrite(pin_buzzer, LOW);
    }
    else
    {
        beepped = false;
        digitalWrite(pin_buzzer, LOW);
    }

    if (SPI_recieved == 1)
    {
        SPI.transfer((void *)&data_pkg, (size_t)sizeof(data_pkg));
        SPI_recieved = 0;
    }
    else if (SPI_recieved == 2)
    {
        SPI.transfer((void *)&stat_pkg, (size_t)sizeof(stat_pkg));
        SPI_recieved = 0;
    }

    Serial.print(stat_pkg.st_emg);
    Serial.print(",");
    Serial.println(stat_pkg.st_acc);
}
