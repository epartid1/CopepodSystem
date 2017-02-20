
 /* 
 * HARDWARE:
 * -> DS3231 I2C RTC
 *    Wiring:
 *      Power 2.3-5.5 V, we are using 5V from Arduino regulator
 *      GND common ground
 *      I2C Logic Pins:
 *        SCL - I2C clock pin, 10K pullup to Vin, connect to A5
 *        SDA - I2C data pin, 10K pullup to Vin, connect to A4
 *    - default I2C address of 0x68 that cannot be changed
 *    
 *    Resources: https://learn.adafruit.com/adafruit-ds3231-precision-rtc-breakout/
 *    
 * -> 5V ready Micro-SD Breakout Board
 *    Wiring:
 *      Power to 5V, Arduino supply
 *      GND common ground
 *      CS to D10
 *      DI to D11
 *      DO to D12
 *      CLK to D13
 *    
 *    Resources: https://learn.adafruit.com/adafruit-micro-sd-breakout-board-card-tutorial/intro
 *      
 * -> MAX31855 Thermocouple Chip
 *    Wiring:
 *      Power to 5V, Arduino supply
 *      GND common ground
 *      DO (data out) to D3
 *      CS (chip select) to D4
 *      CLK (clock) to D5
 *    
 *    Resources: https://learn.adafruit.com/thermocouple/
 *      
 * -> ADC, ADS1115 16-bit I2C and 4-channel with 2/3-16x gain
 *    Wiring:  
 *      Power to 5V, Arduino supply
 *      GND common ground
 *      SCL to SCL on board (top pin on digital column)
 *      SDA to SDA on board (2nd from top pin on digital column)
 *      ADDR to common ground
 *    Functions:  
 *      (comment from singleended example code)
 *      The ADC gain can be changed via the following functions, but be careful not to
 *      exceed VDD +0.3V max or to exceed upper and lower limits if you adjust the input
 *      range. 
 *       ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 0.1875mV (default)
 *       ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 0.125mV 
 *       ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 0.0625mV
 *       ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.03125mV
 *       ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.015625mV
 *       ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.0078125mV
 *       
 *      Resource: https://www.adafruit.com/product/1085
 *       
 *  -> RS-232 Max3232 breakout board
 *    Wiring:
 *      3V-5.5V to 5V Arduino Source
 *      GND to CMN GND
 *      R1IN to O2Probe solid brown (Tx line)
 *      R1OUT to D9 (Software Serial Rx line)
 *      
 *      Resource: http://codeandlife.com/2012/04/12/3-3v-uart-with-max3232cpe/
 *   
 *   
 *   OTHER RESOURCES:
 *   Notes for inclusion of capacitors: http://efundies.com/guides/fundamentals/intro/intro_to_capacitors/intro_to_capacitors_page_2.htm
 *   
 */

/*  LIBRARIES  */
#include <Wire.h>
// Date and time functions using a DS3231 RTC connected via I2C
#include "RTClib_Tig.h"
//SD card communicates through SPI using library SdFat
#include <SPI.h>
#include <SdFat.h>
//Thermocouple library for Max31855
#include "Adafruit_MAX31855.h"
//ADC library for ADS1115, 16-bit and 4 channel (we use one here) connected via I2C
#include <Adafruit_ADS1015.h>
//To create software serial to communicate with the DO probe by RS232
#include <SoftwareSerial.h>

/*  GLOBAL VARIABLES  */
RTC_DS3231 rtc; // Real-time clock
SdFat sd; //SD card
SdFile myFile; //file to manipulate on SD
const int sdCS = 10; //CS is pin 10 for SD handling
Adafruit_ADS1115 adc; //ADC chip
int Model;
int SerialNum;
float DOmgLSet;
SoftwareSerial doProbe(9,8); // RX, TX lines, Dissolved Oxygen probe

//thermocouple
const int thermoDO = 7; //data out to D7
const int thermoCS = 6; //data out to D6
const int thermoCLK = 5; //data out to D5
Adafruit_MAX31855 thermocouple(thermoCLK, thermoCS, thermoDO);

const int milliDelay = 2000; //delay time between readings in milliseconds
bool serialEcho = 1; //1 to write information to Serial for troubleshooting
                     //0 to supress

//gas control
const int O2valve = 4;
double doLow = 6.0;
double doHigh = 6.1;

const int N2valve = 3;

const int CO2valve = 2;
double phLow = 1.15;
double phHigh = 1.2;

//state machine
#define OFF 0 //state for O2 solenoid valve closed
#define ON 1 //state for O2 solenoid valve open
#define SHUTDOWN 2 //state for error shutdown of system
//start with all valves closed
int O2state = OFF;
int CO2state = OFF;
int N2state = OFF;



////////////////////////////////////////////////
////    SET-UP   ////
////////////////////////////////////////////////
void setup() {
  Serial.begin(4800); //communicates with computer via Serial port
  doProbe.begin(9600); //communicates with DO probe
  pinMode(O2valve, OUTPUT); //control pin for O2 solenoid valve
  pinMode(N2valve, OUTPUT); //control pin for O2 solenoid valve
  pinMode(CO2valve, OUTPUT); //control pin for O2 solenoid valve

  delay(2000); // wait for console opening
  if (serialEcho) {
    Serial.println("Commencing set-up...");
  }

  /*  Set-up RTC  */
  //if RTC is unresponsive, code enters indefinite loop, does not continue "loop"
  if (! rtc.begin()) {
    Serial.println("couldn't find RTC, stopping set-up.");
    while (1); 
  }

  //Test removing once battery is added
  DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
  rtc.adjust(compileTime); //remove once battery added
  Serial.println("");
  Serial.print("setting RTC to compile time:\t");
  printTimeToSerial(compileTime);
  
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, lets set the time!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  if (serialEcho) {
    Serial.println(", rtc set-up...");
  }

  /*  Set-up SD Card  */
  // Initialize SdFat or print a detailed error message and halt
  // Use half speed like the native library.
  // change to SPI_FULL_SPEED for more performance.
  if (!sd.begin(sdCS, SPI_HALF_SPEED)) {
    sd.initErrorHalt();
  }
  if (serialEcho) {
    Serial.println("SD card set-up...");
  }
  //Delete current test.txt file if present
  if (sd.exists("test.txt")) {
    sd.remove("test.txt");
  }
  //Start file and print header line
  if (!myFile.open("test.txt", O_RDWR | O_CREAT | O_AT_END)) {
    sd.errorHalt("opening test.txt for write failed");
  }
  myFile.println ("date \t time \t temp_degC \t pH_rawV \t O2_mgL");
  myFile.close();

  /*  Set-up ADC  */
  adc.begin();
  adc.setGain(GAIN_ONE);

  /*  Begin reading DO Probe  */
  Serial.println("Beginning reading...");
  WaitforSerialChar('M');

//  Print O2 probe info if desired.
//  Model = doProbe.parseInt();
//  SerialNum = doProbe.parseInt();
//  Serial.print("Model: ");
//  Serial.println(Model);
//  Serial.print("SerialNum: ");
//  Serial.println(SerialNum);

  /*  Set-up complete */
  if (serialEcho) {
    Serial.println("set-up complete.");
  }
}



////////////////////////////////////////////////
////    LOOP   ////
////////////////////////////////////////////////
void loop() {
  DateTime curr = rtc.now();

  // open the file for write at end like the Native SD library
  // print error and end execution if unable to open file 
  // ** (does not produce error if card is missing)
  if (!myFile.open("test.txt", O_RDWR | O_CREAT | O_AT_END)) {
    sd.errorHalt("opening test.txt for write failed");
  }
  
  if (serialEcho) {
    printTimeToSerial (curr);
  }

  printTimeToSD (curr);

  double c = thermocouple.readCelsius();
   if (isnan(c)) {
     Serial.println("Something wrong with thermocouple!");
   } else {
     if (serialEcho) printTempToSerial (c);
     printTempToSD(c);
   }

  double phMeas = getpH();
  printpHToSD(phMeas);
  if(serialEcho) {
    printpHToSerial(phMeas);
  }
  
  // Read from O2 Probe
  double doMeas = getDOmgL(); //get DO measurement in mg/L
  printDOmgLToSD(doMeas);
  if (serialEcho) {
    printDOmgLToSerial(doMeas);
  }

  switch(O2state) {
    case OFF:
      if(doMeas < doLow) {
        O2state = ON;
        digitalWrite(O2valve, HIGH); //open solenoid valve
        Serial.print("\t");
        Serial.print("O2 ON");
      }
      break; //OFF
      
    case ON:
      if(doMeas > doHigh) {
        O2state = OFF;
        digitalWrite(O2valve, LOW); //close solenoid valve 
        Serial.print("\t"); 
        Serial.print("O2 OFF");
      }
      break; //ON
    
    case SHUTDOWN:
      digitalWrite(O2valve, LOW); //open solenoid valve
      break; //SHUTDOWN

  }//O2state state machine

  switch(CO2state) {
     case OFF:
      if(phMeas > phHigh) {
        CO2state = ON;
        digitalWrite(CO2valve, HIGH); //open solenoid valve  
        Serial.print("\t");
        Serial.print("CO2 ON");
      }
      break; //OFF
      
      case ON:
      if(phMeas < phLow) {
        CO2state = OFF;
        digitalWrite(CO2valve, LOW); //open solenoid valve
        Serial.print("\t");
        Serial.print("CO2 OFF");
      }
      break; //ON

    case SHUTDOWN:
      digitalWrite(CO2valve, LOW); //open solenoid valve
      break; //SHUTDOWN
  }//CO2state state machine

    switch(N2state) {
     case OFF:
      if(phMeas < phLow || doMeas > doHigh) {
        N2state = ON;
        digitalWrite(N2valve, HIGH); //open solenoid valve  
        Serial.print("\t");
        Serial.print("N2 ON");
      }
      break; //OFF
      
      case ON:
      if(phMeas > phLow && doMeas < doHigh) {
        N2state = OFF;
        digitalWrite(N2valve, LOW); //open solenoid valve
        Serial.print("\t");
        Serial.print("N2 OFF");
      }
      break; //ON

    case SHUTDOWN:
      digitalWrite(N2valve, LOW); //open solenoid valve
      break; //SHUTDOWN
  }//N2state state machine

  myFile.println();
  if(serialEcho) {
    Serial.println();
  }
  myFile.close();
  delay (milliDelay);

  if (O2state == SHUTDOWN) {
    Serial.println("error: SHUTTING DOWN");
    while (1); //enter permenant while loop
  }
}



////////////////////////////////////////////////
////    FUNCTIONS   ////
////////////////////////////////////////////////
/*
 * Prints current time to SD.
 * 
 * @parameters Pass current DateTime object to be printed to SD
*/
static void printTimeToSD (DateTime curr) {
  myFile.print(curr.year(), DEC);
  myFile.print('/');
  myFile.print(curr.month(), DEC);
  myFile.print('/');
  myFile.print(curr.day(), DEC);
  myFile.print('\t');
  myFile.print(curr.hour(), DEC);
  myFile.print(':');
  myFile.print(curr.minute(), DEC);
  myFile.print(':');
  myFile.print(curr.second(), DEC);
}

/*
 * Prints current time to Serial.
 * 
 * @parameters Pass current DateTime object to be printed to SD.
*/
static void printTimeToSerial (DateTime curr) {
  Serial.print(curr.year(), DEC);
  Serial.print('/');
  Serial.print(curr.month(), DEC);
  Serial.print('/');
  Serial.print(curr.day(), DEC);
  Serial.print('\t');
  Serial.print(curr.hour(), DEC);
  Serial.print(':');
  Serial.print(curr.minute(), DEC);
  Serial.print(':');
  Serial.print(curr.second(), DEC);
}

/*
 * Prints temperature to Serial.
 * 
 * @parameters Pass temperature as double to be printed.
*/
static void printTempToSD (double temp) {
  myFile.print('\t');
  myFile.print(temp);
}
/*
 * Prints temperature to Serial.
 * 
 * @parameters Pass temperature as double to be printed.
*/
static void printTempToSerial (double temp) {
  Serial.print("\t C = ");
  Serial.print(temp);
}

/*
 * Prints pH to SD.
 * 
 * @parameters Pass pH as double to be printed.
*/
static void printpHToSD (double pH) {
  myFile.print('\t');
  myFile.print(pH);
}
/*
 * Prints pH to Serial.
 * 
 * @parameters Pass pH as double to be printed.
*/
static void printpHToSerial (double pH) {
  Serial.print('\t');
  Serial.print("pH (V from ADC) = "); 
  Serial.print(pH);
}
/*
 * Prints DO to SD in units of uMol.
 * 
 * @parameters Pass DO in uMol as double to be printed.
*/
static void printDOuMolToSD (double measure) {
  myFile.print("\t");
  myFile.print(measure);
}
/*
 * Prints DO to Serial in units of uMol.
 * 
 * @parameters Pass DO in uMol as double to be printed.
*/
static void printDOuMolToSerial (double measure) {
  Serial.print("\t DO (uMol) = ");
  Serial.print(measure);
}
/*
 * Prints DO to SD in units of mg/L.
 * 
 * @parameters Pass DO in mg/L as double to be printed.
*/
static void printDOmgLToSD (double measure) {
  myFile.print("\t");
  myFile.print(measure);
}
/*
 * Prints DO to Serial in units of mg/L.
 * 
 * @parameters Pass DO in mg/L as double to be printed.
*/
static void printDOmgLToSerial (double measure) {
  Serial.print("\t DO (mg/L) = ");
  Serial.print(measure);
}

/*
 * Returns double of pH voltage being read off of A0 of ADC.
 * 
 * @parameters None
 * @return double of pH meter voltage **need to add logic to return pH**
*/
static double getpH () {
   int16_t pHRaw; //16-bit int dedicated to pH readings to be taken on A2
   pHRaw = adc.readADC_SingleEnded(2);
   double pHVolt = pHRaw *0.000125; //with 1x gain 1 bit = 0.125mV
   return pHVolt;
}

/*
 * Haults execution until the DO probe serial passes an 'M' at the beginning of MEASUREMENT,
 * which begins the next reading. Can pass any character as parameter start, does not need
 * to be 'M'
 * 
 * @parameters char start = the character to wait for
 */
void WaitforSerialChar(char start){
  char currChar = doProbe.read();
  while (currChar != (start)){
    currChar = doProbe.read();
  }
}

/*
 * Parse RS232 data from O2 probe to get DO in uMol from the Serial stream.
 * 
 * @parameters N/A
 * @return Returns value of DO reading in uMol.
 */
double getDOuMol(){
  WaitforSerialChar('M');
  doProbe.parseInt();
  doProbe.parseInt();
  float DOuMolMeas = doProbe.parseFloat();
}
/*
 * Calls getDOuMol and converts value of O2 from uMol to return value in mg/L.
 * 
 * @parameters N/A
 * @return Returns value of DO reading in mg/L
 */
double getDOmgL(){
  float DOuMolMeas = getDOuMol();
  float DOmgLMeas = DOuMolMeas*0.031998;  //convert uMol to mg/L
  return DOmgLMeas;
}
