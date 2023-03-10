/*
 Weather Shield Otto + o2 + Dust + Light

 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

 Much of this is based on Mike Grusin's USB Weather Board code: https://www.sparkfun.com/products/10586

 This is a more advanced example of how to utilize every aspect of the weather shield. See the basic
 example if you're just getting started.

 This code reads all the various sensors (wind speed, direction, rain gauge, humidity, pressure, light, batt_lvl)
 and reports it over the serial comm port. This can be easily routed to a datalogger (such as OpenLog) or
 a wireless transmitter (such as Electric Imp).

 This example code assumes the GPS module is not used.

 */

#include <Wire.h>                             //I2C needed for sensors
#include "SparkFunMPL3115A2.h"                //Pressure sensor - Search "SparkFun MPL3115" and install from Library Manager
#include "SparkFun_Si7021_Breakout_Library.h" //Humidity sensor - Search "SparkFun Si7021" and install from Library Manager

#include "Adafruit_PM25AQI.h" //dust particle sensor
#include "Spi.h"              //dust particle sensor

#include <Adafruit_Sensor.h> //Light
#include "Adafruit_TSL2591.h"//Light

// This Part is for the oxigen Sensor
const float VRefer = 3.3; // voltage of adc reference
const int pinAdc = A2;
// This Part is for the oxigen Sensor

Adafruit_PM25AQI aqi = Adafruit_PM25AQI(); // dust particle sensor
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);

MPL3115A2 myPressure; // Create an instance of the pressure sensor
Weather myHumidity;   // Create an instance of the humidity sensor

// Hardware pin definitions
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//  digital I/O pins
const byte WSPEED = 3;
const byte RAIN = 2;
const byte STAT1 = 7;
const byte STAT2 = 8;

// analog I/O pins
const byte REFERENCE_3V3 = A3;
const byte LIGHT = A1;
const byte WDIR = A0;
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond;  // The millis counter to see when a second rolls by
byte seconds;     // When it hits 60, increase the current minute
byte seconds_2m;  // Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes;     // Keeps track of where we are in various arrays of data
byte minutes_10m; // Keeps track of where we are in wind gust/dir over last 10 minutes array of data

long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;

// We need to keep track of the following variables:
// Wind speed/dir each update (no storage)
// Wind gust/dir over the day (no storage)
// Wind speed/dir, avg over 2 minutes (store 1 per second)
// Wind gust/dir over last 10 minutes (store 1 per minute)
// Rain over the past hour (store 1 per minute)
// Total rain over date (store one per day)

byte windspdavg[120]; // 120 bytes to keep track of 2 minute average

#define WIND_DIR_AVG_SIZE 120
int winddiravg[WIND_DIR_AVG_SIZE]; // 120 ints to keep track of 2 minute average
float windgust_10m[10];            // 10 floats to keep track of 10 minute max
int windgustdirection_10m[10];     // 10 ints to keep track of 10 minute max
volatile float rainHour[60];       // 60 floating numbers to keep track of 60 minutes of rain

// These are all the weather values that wunderground expects:
int winddir = 0;                // [0-360 instantaneous wind direction]
float windspeedmph = 0;         // [mph instantaneous wind speed]
float windgustmph = 0;          // [mph current wind gust, using software specific time period]
int windgustdir = 0;            // [0-360 using software specific time period]
float windspdmph_avg2m = 0;     // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0;          // [0-360 2 minute average wind direction]
float windgustmph_10m = 0;      // [mph past 10 minutes wind gust mph ]
int windgustdir_10m = 0;        // [0-360 past 10 minutes wind gust direction]
float humidity = 0;             // [%]
float tempf = 0;                // [temperature F]
float temp_h = 0;               // Temperature F  Added Galo.
float rainin = 0;               // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
volatile float dailyrainin = 0; // [rain inches so far today in local time]
// float baromin = 30.03;// [barom in] - It's hard to calculate baromin locally, do this in the agent
float pressure = 0;
// float dewptf; // [dewpoint F] - It's hard to calculate dewpoint locally, do this in the agent

// float batt_lvl = 11.8; //[analog value from 0 to 1023]
float light_lvl = 455; //[analog value from 0 to 1023]

float o2concentration = 0; //[]

// volatiles are subject to modification by IRQs
volatile unsigned long raintime, rainlast, raininterval, rain;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
    raintime = millis();                // grab current time
    raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
    {
        dailyrainin += 0.011;       // Each dump is 0.011" of water
        rainHour[minutes] += 0.011; // Increase this minute's amount of rain

        rainlast = raintime; // set up for next event
    }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
    if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
    {
        lastWindIRQ = millis(); // Grab the current time
        windClicks++;           // There is 1.492MPH for each click per second.
    }
}

/**************************************************************************/
/*
    Configures the gain and integration time for the TSL2591
*/
/**************************************************************************/
void configureSensor(void)
{
  // You can change the gain on the fly, to adapt to brighter/dimmer light situations
  //tsl.setGain(TSL2591_GAIN_LOW);    // 1x gain (bright light)
  tsl.setGain(TSL2591_GAIN_MED);      // 25x gain
  //tsl.setGain(TSL2591_GAIN_HIGH);   // 428x gain
  
  // Changing the integration time gives you a longer time over which to sense light
  // longer timelines are slower, but are good in very low light situtations!
  //tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_200MS);
  tsl.setTiming(TSL2591_INTEGRATIONTIME_300MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_400MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_500MS);
  // tsl.setTiming(TSL2591_INTEGRATIONTIME_600MS);  // longest integration time (dim light)

  /* Display the gain and integration time for reference sake */  
  Serial.println(F("------------------------------------"));
  Serial.print  (F("Gain:         "));
  tsl2591Gain_t gain = tsl.getGain();
  switch(gain)
  {
    case TSL2591_GAIN_LOW:
      Serial.println(F("1x (Low)"));
      break;
    case TSL2591_GAIN_MED:
      Serial.println(F("25x (Medium)"));
      break;
    case TSL2591_GAIN_HIGH:
      Serial.println(F("428x (High)"));
      break;
    case TSL2591_GAIN_MAX:
      Serial.println(F("9876x (Max)"));
      break;
  }
  Serial.print  (F("Timing:       "));
  Serial.print((tsl.getTiming() + 1) * 100, DEC); 
  Serial.println(F(" ms"));
  Serial.println(F("------------------------------------"));
  Serial.println(F(""));
}

void setup()
{
    Serial.begin(9600);
    Serial.println("Otto Weather Shield");
    Serial.println("Adafruit PMSA003I Air Quality Sensor");
    Serial.println("Grove - Oxygen Sensor(MIX8410) ");

    if (!aqi.begin_I2C())
    { // connect to the sensor over I2C
        // if (! aqi.begin_UART(&Serial1)) { // connect to the sensor over hardware serial
        // if (! aqi.begin_UART(&pmSerial)) { // connect to the sensor over software serial
        Serial.println("Could not find PM 2.5 sensor! -Dust-");
        while (1)
            delay(10);
    }

    if (tsl.begin()){
        Serial.println(F("Found a TSL2591 sensor"));
    } 
    else {
        Serial.println(F("No sensor found ... check your wiring?"));
        while (1);
    }
    configureSensor();
    delay(1000);

    Serial.println("PM25 found! -Dust-");

    delay(1000);
    pinMode(STAT2, OUTPUT); // Status LED Green
    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP);   // input from wind meters rain gauge sensor
    pinMode(REFERENCE_3V3, INPUT);
    pinMode(LIGHT, INPUT);

    // Configure the pressure sensor
    myPressure.begin();              // Get sensor online
    myPressure.setModeBarometer();   // Measure pressure in Pascals from 20 to 110 kPa
    myPressure.setOversampleRate(7); // Set Oversample to the recommended 128
    myPressure.enableEventFlags();   // Enable all three pressure and temp event flags

    // Configure the humidity sensor
    myHumidity.begin();
    seconds = 0;
    lastSecond = millis();

    // attach external interrupt pins to IRQ functions
    attachInterrupt(0, rainIRQ, FALLING);
    attachInterrupt(1, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();
    Serial.println(" Otto Weather Shield online!");
}

void loop()
{
    if (millis() - lastSecond >= 2000)
    {
        digitalWrite(STAT1, HIGH); // Blink stat LED
        lastSecond += 2000;
        // Take a speed and direction reading every second for 2 minute average
        if (++seconds_2m > 119)
            seconds_2m = 0;
        // Calc the wind speed and direction every second for 120 second to get 2 minute average
        float currentSpeed = get_wind_speed();
        windspeedmph = currentSpeed; // update global variable for windspeed when using the printWeather() function
        // float currentSpeed = random(5); //For testing
        int currentDirection = get_wind_direction();
        windspdavg[seconds_2m] = (int)currentSpeed;
        winddiravg[seconds_2m] = currentDirection;
        // if(seconds_2m % 10 == 0) displayArrays(); //For testing
        // Check to see if this is a gust for the minute
        if (currentSpeed > windgust_10m[minutes_10m])
        {
            windgust_10m[minutes_10m] = currentSpeed;
            windgustdirection_10m[minutes_10m] = currentDirection;
        }

        // Check to see if this is a gust for the day
        if (currentSpeed > windgustmph)
        {
            windgustmph = currentSpeed;
            windgustdir = currentDirection;
        }
        if (++seconds > 59)
        {
            seconds = 0;

            if (++minutes > 59)
                minutes = 0;
            if (++minutes_10m > 9)
                minutes_10m = 0;

            rainHour[minutes] = 0;         // Zero out this minute's rainfall amount
            windgust_10m[minutes_10m] = 0; // Zero out this minute's gust
        }

        // Report all readings every second
        printWeather();
        digitalWrite(STAT1, LOW); // Turn off stat LED
    }

    delay(100);
}

// Calculates each of the variables that wunderground is expecting
void calcWeather()
{
    // Calc winddir
    winddir = get_wind_direction();

    // Calc windspeed
    // windspeedmph = get_wind_speed(); //This is calculated in the main loop on line 185

    // Calc windgustmph
    // Calc windgustdir
    // These are calculated in the main loop

    // Calc windspdmph_avg2m
    float temp = 0;
    for (int i = 0; i < 120; i++)
        temp += windspdavg[i];
    temp /= 120.0;
    windspdmph_avg2m = temp;

    // Calc winddir_avg2m, Wind Direction
    // You can't just take the average. Google "mean of circular quantities" for more info
    // We will use the Mitsuta method because it doesn't require trig functions
    // And because it sounds cool.
    // Based on: http://abelian.org/vlf/bearings.html
    // Based on: http://stackoverflow.com/questions/1813483/averaging-angles-again
    long sum = winddiravg[0];
    int D = winddiravg[0];
    for (int i = 1; i < WIND_DIR_AVG_SIZE; i++)
    {
        int delta = winddiravg[i] - D;

        if (delta < -180)
            D += delta + 360;
        else if (delta > 180)
            D += delta - 360;
        else
            D += delta;

        sum += D;
    }
    winddir_avg2m = sum / WIND_DIR_AVG_SIZE;
    if (winddir_avg2m >= 360)
        winddir_avg2m -= 360;
    if (winddir_avg2m < 0)
        winddir_avg2m += 360;

    // Calc windgustmph_10m
    // Calc windgustdir_10m
    // Find the largest windgust in the last 10 minutes
    windgustmph_10m = 0;
    windgustdir_10m = 0;
    // Step through the 10 minutes
    for (int i = 0; i < 10; i++)
    {
        if (windgust_10m[i] > windgustmph_10m)
        {
            windgustmph_10m = windgust_10m[i];
            windgustdir_10m = windgustdirection_10m[i];
        }
    }

    // Calc humidity
    humidity = myHumidity.getRH();
    temp_h = myHumidity.getTempF(); // Galo modified
    // float temp_h = myHumidity.readTemperature();
    // Serial.print(" TempH:");
    // Serial.print(temp_h, 2);

    // Calc tempf from pressure sensor
    tempf = myPressure.readTempF();
    // Serial.print(" TempP:");
    // Serial.print(tempf, 2);

    // Total rainfall for the day is calculated within the interrupt
    // Calculate amount of rainfall for the last 60 minutes
    rainin = 0;
    for (int i = 0; i < 60; i++)
        rainin += rainHour[i];

    // Calc pressure
    pressure = myPressure.readPressure();

    // Calc dewptf

    // Calc light level
    light_lvl = get_light_level();

    // Calc battery level
    // batt_lvl = get_battery_level();
}

// Returns the voltage of the light sensor based on the 3.3V rail
// This allows us to ignore what VCC might be (an Arduino plugged into USB has VCC of 4.5 to 5.2V)
float get_light_level()
{
    float operatingVoltage = analogRead(REFERENCE_3V3);

    float lightSensor = analogRead(LIGHT);

    operatingVoltage = 3.3 / operatingVoltage; // The reference voltage is 3.3V

    lightSensor = operatingVoltage * lightSensor;

    return (lightSensor);
}

// Returns the instataneous wind speed
float get_wind_speed()
{
    float deltaTime = millis() - lastWindCheck; // 750ms

    deltaTime /= 1000.0; // Covert to seconds

    float windSpeed = (float)windClicks / deltaTime; // 3 / 0.750s = 4

    windClicks = 0; // Reset and start watching for new wind
    lastWindCheck = millis();

    windSpeed *= 1.492; // 4 * 1.492 = 5.968MPH

    /* Serial.println();
     Serial.print("Windspeed:");
     Serial.println(windSpeed);*/

    return (windSpeed);
}

// Read the wind direction sensor, return heading in degrees
int get_wind_direction()
{
    unsigned int adc;

    adc = analogRead(WDIR); // get the current reading from the sensor

    // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
    // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
    // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

    if (adc < 380)
        return (113);
    if (adc < 393)
        return (68);
    if (adc < 414)
        return (90);
    if (adc < 456)
        return (158);
    if (adc < 508)
        return (135);
    if (adc < 551)
        return (203);
    if (adc < 615)
        return (180);
    if (adc < 680)
        return (23);
    if (adc < 746)
        return (45);
    if (adc < 801)
        return (248);
    if (adc < 833)
        return (225);
    if (adc < 878)
        return (338);
    if (adc < 913)
        return (0);
    if (adc < 940)
        return (293);
    if (adc < 967)
        return (315);
    if (adc < 990)
        return (270);
    return (-1); // error, disconnected?
}
// o2 calculation code

float readO2Vout()
{
    long sum = 0;
    for (int i = 0; i < 32; i++)
    {
        sum += analogRead(pinAdc);
    }

    sum >>= 5;

    float MeasuredVout = sum * (VRefer / 1023.0);
    return MeasuredVout;
}

float readConcentration()
{
    // Vout samples are with reference to 3.3V
    float MeasuredVout = readO2Vout();

    // float Concentration = FmultiMap(MeasuredVout, VoutArray,O2ConArray, 6);
    // when its output voltage is 2.0V,
    float Concentration = MeasuredVout * 0.21 / 2.0;
    float Concentration_Percentage = Concentration * 100;

    return Concentration_Percentage;
}
// o2 calculation code end

// Prints the various variables directly to the port
// I don't like the way this function is written but Arduino doesn't support floats under sprintf
void printWeather()
{
    calcWeather(); // Go calc all the various sensors
    Serial.print("$");
    Serial.print(winddir);
    Serial.print(",");
    Serial.print(windspeedmph, 1); // windspeedmph
    Serial.print(",");
    Serial.print(windgustmph, 1);
    Serial.print(",");
    Serial.print(windgustdir);
    Serial.print(",");
    Serial.print(windspdmph_avg2m, 1);
    Serial.print(",");
    Serial.print(winddir_avg2m);
    Serial.print(",");
    Serial.print(windgustmph_10m, 1);
    Serial.print(",");
    Serial.print(windgustdir_10m);
    Serial.print(",");
    Serial.print(humidity, 1);
    Serial.print(",");
    Serial.print(temp_h, 2); //(tempf, 1);
    Serial.print(",");
    Serial.print(rainin, 2);
    Serial.print(",");
    Serial.print(dailyrainin, 2);
    Serial.print(",");
    Serial.print(pressure, 2);
    Serial.print(",");
    Serial.print(light_lvl, 2);
    Serial.print(",");
    o2concentration = readConcentration();
    Serial.print(o2concentration, 2);

    // code For DustLoop
    PM25_AQI_Data data;

    if (!aqi.read(&data))
    {
        // Serial.println("Could not read from AQI");
        delay(500); // try again in a bit!
        return;
    }


    Serial.print(",");
    Serial.print(data.pm10_standard);
    Serial.print(",");
    Serial.print(data.pm25_standard);
    Serial.print(",");
    Serial.print(data.pm100_standard);
    Serial.print(",");

    uint32_t lum = tsl.getFullLuminosity();
    uint16_t ir, full;
    ir = lum >> 16;
    full = lum & 0xFFFF;
    Serial.print(ir); Serial.print(","); 
    Serial.print(full); Serial.print(F(","));
    Serial.print(full - ir); Serial.print(F(","));
    Serial.print(tsl.calculateLux(full, ir), 6);
    Serial.println("#"); // end of serial string
}
