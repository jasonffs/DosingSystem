#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Ethernet.h>
#include <NTPClient.h>
#include <EthernetUdp.h>
#include <NtpSdTimeSupplier.h>
#include <ApplicationMonitor.h>
#include <limits.h>
#include <Adafruit_MAX31865.h>
#include <PushingBoxService.h>

#include "DEV_Config.h"
#include "Deadline.h"
#include <Adafruit_GFX.h>
#include <Waveshare4InchTftShield.h>

String version = "2020.05.04.8 Rollback";
/*
Issues;
*/

/************************************************************
                   _       _               _____ _           
     /\           | |     (_)             |  __ (_)          
    /  \   _ __ __| |_   _ _ _ __   ___   | |__) | _ __  ___ 
   / /\ \ | '__/ _` | | | | | '_ \ / _ \  |  ___/ | '_ \/ __|
  / ____ \| | | (_| | |_| | | | | | (_) | | |   | | | | \__ \
 /_/    \_\_|  \__,_|\__,_|_|_| |_|\___/  |_|   |_|_| |_|___/
************************************************************/

/*
    ********* Other pins: *********
    SPI pins:
        MOSI = 11
        MISO = 12
        SCLK = 13
*/

int pinsOut[] =
    {

        10, //Ethernet CS
        4,  //SD CS
        //7,  //Display CS
        //6,  //Display RST
        //5,  //Display DC
        15, //Temp sensor Product CS //fake
        16, //Temp sensor Water CS //fake
        34, //12kW Heater
        35, //24kW Heater
        36, //36kW Heater
        37, //Product Mixer
        38, //Product Pump
        39, //Water Mixer
        40, //Water Pump
        41, //System ready light
        42, //Alarm light on flashing
        44,
        53,

};

int pinsIn[] =
    {
        29, //Display Enter
        30, //Display left button
        31, //Display right button
        32, //Display up button
        33, //Display down button
};

struct Sensors
{
    double temp[2]; //Prod, Water
    float mixer[2]; //Prod, Water
};

struct Control
{
    boolean heat[3];   //12kW, 24kW, 36kW
    boolean prod[2];   //mixer ,pump
    boolean water[2];  //mixer, pump
    boolean status[2]; //system ready, alarm
};

struct parameters
{
    short prodTempSetValue;
    short heaterStop[2]; //24kW, 36kW
    short lowAlarm[2];   //prod, water
    short prodTempHysteresis;
    float prodTempBandGap[2]; //Width, offset
    short waterOverTemp;
    int prodFlushPumpDelay;
};

const char *errorMessages[] =
    {
        "No_SD_card",
        "No_Internet_Connection",
        "Product_Mixer_Current_Level_Is_Low",
        "Water_Mixer_Current_Level_Is_Low",
        "Water_Over_Temp",
        "SD_logger_failed",
};

const char *setupMenu[] =
    {
        "Product temperature set value",
        "24kW heater stop value",
        "36kW heater stop value",
        "Water over heat alarm value",
        "Water low alarm value (1-10)",
        "Product low alarm value (1-10)",
        "Product Temp hysteresis time",
        "Product Temp band gap (WIDTH)",
        "Product Temp band gap (OFFSET)",
};

byte mac[] =
    {
        0xA8,
        0x61,
        0x0A,
        0xAE,
        0x56,
        0x9E,
};

using namespace DosingSystem;
const char *timeServer = "time.nist.gov";
const char *pushingBoxHost = "api.pushingbox.com";
const char *devid = "vC4E57EF2C103A27";
const char *fileConfig = "config.txt";
const int httpPort = 80;
long long buttonRecentlyPressed = 0;
long displayTimeOut = 240000; //Display timeout 4 min (logout)
boolean wasButtonPressed = false;
unsigned long startTime = 0;
unsigned long totalTime = 0;
unsigned long tempConUpdate = 0;
unsigned long tempConDelay = 0;

#define numPinsOut (sizeof(pinsOut))
#define numPinsIn (sizeof(pinsIn))
#define numLog (sizeof(dataLogNormal))
#define RREF 430.0
#define RNOMINAL 100.0
/*#define LCD_CS      pinsOut[2]
#define LCD_BL      2
#define LCD_RST     pinsOut[3]
#define LCD_DC      pinsOut[4]*/

auto dispRotation = 3; //display rotation
static short flushtype = 1;
static short menu = 1;
static short mode = 1;
static short menuItemMain = 1;
static short menuItemSetupOp = 1;
static short menuItemSetupMan = 1;
short loopCount = 1;
boolean mainDisplayed = false;
boolean userOpLoggedIn = false;
boolean userManLoggedIn = false;
static short tempPass1 = 0;
static short tempPass2 = 0;
static short tempPass3 = 0;
static short tempPass4 = 0;
static short passIndex = 1;
static short errorIndex = 0;

//Passcode for operator is 7382
//Passcode for manager is 3749

const NtpSdTimeSupplier *timeSupplier_;
struct Sensors sensordata;
struct Control control;
struct parameters Parameters;
struct parameters ParametersTemp;
static boolean errorMessage;

boolean timeAvalible = false;
boolean ethFailed = false;
EthernetUDP ntpUDP_;
NTPClient timeClient_(ntpUDP_, "za.pool.ntp.org", 3600, 120000);

Watchdog::CApplicationMonitor ApplicationMonitor;
Waveshare4InchTftShield Waveshield;
Adafruit_GFX &tft = Waveshield;
Adafruit_MAX31865 max_2 = Adafruit_MAX31865(8, 11, 12, 13); // ss, mosi, miso, sck
Adafruit_MAX31865 max_1 = Adafruit_MAX31865(9, 11, 12, 13); // ss, mosi, miso, sck

// Color definitions
#define ILI9486_BLACK 0x0000       //<   0,   0,   0
#define ILI9486_NAVY 0x000F        //<   0,   0, 123
#define ILI9486_DARKGREEN 0x03E0   //<   0, 125,   0
#define ILI9486_DARKCYAN 0x03EF    //<   0, 125, 123
#define ILI9486_MAROON 0x7800      //< 123,   0,   0
#define ILI9486_PURPLE 0x780F      //< 123,   0, 123
#define ILI9486_OLIVE 0x7BE0       //< 123, 125,   0
#define ILI9486_LIGHTGREY 0xC618   //< 198, 195, 198
#define ILI9486_DARKGREY 0x7BEF    //< 123, 125, 123
#define ILI9486_BLUE 0x001F        //<   0,   0, 255
#define ILI9486_GREEN 0x07E0       //<   0, 255,   0
#define ILI9486_CYAN 0x07FF        //<   0, 255, 255
#define ILI9486_RED 0xF800         //< 255,   0,   0
#define ILI9486_MAGENTA 0xF81F     //< 255,   0, 255
#define ILI9486_YELLOW 0xFFE0      //< 255, 255,   0
#define ILI9486_WHITE 0xFFFF       //< 255, 255, 255
#define ILI9486_ORANGE 0xFD20      //< 255, 165,   0
#define ILI9486_GREENYELLOW 0xAFE5 //< 173, 255,  41
#define ILI9486_PINK 0xFC18        //< 255, 130, 198

bool purgeInit false;
uint8_t purgeCycleStartTime = 0;
uint8_t PurgeCounter = 0;

/***************************************
  _____       _ _   _       _ _          
 |_   _|     (_) | (_)     | (_)         
   | |  _ __  _| |_ _  __ _| |_ _______  
   | | | '_ \| | __| |/ _` | | |_  / _ \ 
  _| |_| | | | | |_| | (_| | | |/ /  __/ 
 |_____|_| |_|_|\__|_|\__,_|_|_/___\___| 
                                         
***************************************/

void setup()
{
    Serial.begin(9600);
    Serial.println();
    Serial.println("VERSION: " + version);
    Serial.println();

    intPins(pinsIn, numPinsIn, false);  //pass pinsIn array through pinmode function
    intPins(pinsOut, numPinsOut, true); //pass pinsOut array through pinmode function
    displaySetup();
    delay(30);
    enable("Disp");
    Serial.println("Pins initialized sucsesfully");
    tft.println("Pins initialized sucsesfully");

    control.status[0] = false;
    control.status[1] = false;
    control.heat[0] = false;
    control.heat[1] = false;
    control.heat[2] = false;
    control.prod[1] = false;
    control.water[1] = false;
    control.water[0] = true;
    control.prod[0] = true;
    outputControl();

    delay(30);
    enable("Disp");
    Serial.println("Initial outputs sucsesfully set");
    tft.println("Initial outputs sucsesfully set");
    delay(30);

    initSD();
    delay(30);
    enable("Disp");
    Serial.println("SD card initialized sucsesfully");
    tft.println("SD card initialized sucsesfully");
    delay(30);

    loadConfiguration(fileConfig, Parameters);
    delay(30);
    enable("Disp");
    Serial.println("Config loaded sucsesfully from SD card");
    tft.println("Config loaded sucsesfully from SD card");

    ParametersTemp.prodFlushPumpDelay = Parameters.prodFlushPumpDelay;
    ParametersTemp.prodTempSetValue = Parameters.prodTempSetValue;
    ParametersTemp.heaterStop[0] = Parameters.heaterStop[0];
    ParametersTemp.heaterStop[1] = Parameters.heaterStop[1];
    ParametersTemp.lowAlarm[0] = Parameters.lowAlarm[0];
    ParametersTemp.lowAlarm[1] = Parameters.lowAlarm[1];
    ParametersTemp.prodTempHysteresis = Parameters.prodTempHysteresis;
    ParametersTemp.prodTempBandGap[0] = Parameters.prodTempBandGap[0];
    ParametersTemp.prodTempBandGap[1] = Parameters.prodTempBandGap[1];
    ParametersTemp.waterOverTemp = Parameters.waterOverTemp;
    enable("Disp");
    Serial.println("Sucsesfully copied config data to temporary struct");
    tft.println("Sucsesfully copied config data to temporary struct");

    totalTime = Parameters.prodFlushPumpDelay * 10240;
    tempConDelay = Parameters.prodTempHysteresis * 10240;
    tempConUpdate = millis();

    delay(30);
    tempSensorsInit();
    enable("Disp");
    Serial.println("Temperature sensors initialized sucsesfully");
    tft.println("Temperature sensors initialized sucsesfully");
    delay(30);

    ethFailed = startEthernet();
    if (ethFailed == false)
    {
        tft.println("Network connection available");
    }
    else if (ethFailed == true)
    {
        tft.println("No network connection, will try later");
    }

    timeClient_.setUpdateInterval(Duration::hours(24).toMilliSeconds());
    timeSupplier_ = NtpSdTimeSupplier::begin(timeClient_);
    ApplicationMonitor.EnableWatchdog(Watchdog::CApplicationMonitor::Timeout_8s);
    ApplicationMonitor.IAmAlive();
    enable("Disp");
    Serial.println("Watchdog started sucsesfully");
    tft.println("Watchdog started sucsesfully");

    ApplicationMonitor.IAmAlive();
    delay(4000);

    readSensors();
    checkForErrors();
    enable("Disp");
    Serial.println("Read sensors and checked for initial errors sucsesfully");
    tft.println("Read sensors and checked for initial errors sucsesfully");

    control.prod[1] = true;  //enables pump
    control.water[1] = true; //enables pump
    enable("Disp");
    Serial.println("Started pumps");
    tft.println("Started pumps");

    ApplicationMonitor.IAmAlive();
    tft.fillScreen(ILI9486_WHITE);
    productionStatsDisp();
}

void tempSensorsInit()
{
    max_1.begin(MAX31865_4WIRE); // set to 2WIRE or 4WIRE as necessary
    max_2.begin(MAX31865_4WIRE); // set to 2WIRE or 4WIRE as necessary
}

void displaySetup()
{
    //digitalWrite(7, HIGH);
    enable("Disp");
    SPI.begin();
    Waveshield.begin();
    Serial.println("Display initialized sucsesfully");
    tft.invertDisplay(false);
    tft.setRotation(dispRotation);
    tft.fillScreen(ILI9486_WHITE);
    tft.setTextSize(1);
    tft.setCursor(0, 5);
    tft.setTextColor(ILI9486_BLACK);
    tft.println("Display start");
}

void initSD()
{
    enable("SD");
    boolean sdErrorDisplayed = false;
    while (!SD.begin(pinsOut[1]))
    {
        //We could use error codes...
        Serial.println("SD Card failed or not present.");
        if (sdErrorDisplayed == false)
        {
            tft.println("SD Card failed or not present");
            sdErrorDisplayed = true;
        }
        delay(1000);
    }
    delay(1000); //Ensure SD is settled. Allows for hot plugin
    Serial.println("SD Card initialized.");
}

void loadConfiguration(const char *filename, parameters &Parameters)
{
    // Open file for reading
    enable("SD");
    File file = SD.open(filename);

    // Allocate the memory pool on the stack.
    // Don't forget to change the capacity to match your JSON document.
    // Use arduinojson.org/assistant to compute the capacity.
    StaticJsonBuffer<512> jsonBuffer;

    // Parse the root object
    JsonObject &root = jsonBuffer.parseObject(file);

    if (!root.success())
        Serial.println(F("Failed to read file, using default configuration"));

    // Copy values from the JsonObject to the Config
    Parameters.prodFlushPumpDelay = root["prodPumpFlushDelay"] | 30;
    Parameters.prodTempSetValue = root["prodTempSetValue"] | 85;
    Parameters.waterOverTemp = root["waterOverTemp"] | 92;
    Parameters.prodTempHysteresis = root["prodTempHysteresis"] | 35;
    Parameters.heaterStop[0] = root["heat24stop"] | 70;
    Parameters.heaterStop[1] = root["heat36stop"] | 60;
    Parameters.lowAlarm[0] = root["prodLowAlarm"] | 4;
    Parameters.lowAlarm[1] = root["waterLowAlarm"] | 4;
    Parameters.prodTempBandGap[0] = root["prodBandGap_width"] | 2;
    Parameters.prodTempBandGap[1] = root["prodBandGap_offset"] | 1;

    Serial.println();
    Serial.println("prodFlushPumpDelay: " + String(Parameters.prodFlushPumpDelay));
    Serial.println("prodTempSetValue: " + String(Parameters.prodTempSetValue));
    Serial.println("waterOverTemp: " + String(Parameters.waterOverTemp));
    Serial.println("prodTempHysteresis: " + String(Parameters.prodTempHysteresis));
    Serial.println("heaterStop 24kW: " + String(Parameters.heaterStop[0]));
    Serial.println("heaterStop 36kW: " + String(Parameters.heaterStop[1]));
    Serial.println("lowAlarm Prod mixer: " + String(Parameters.lowAlarm[0]));
    Serial.println("lowAlarm water mixer: " + String(Parameters.lowAlarm[1]));
    Serial.println("prodTempBandGap Width: " + String(Parameters.prodTempBandGap[0]));
    Serial.println("prodTempBandGap offset: " + String(Parameters.prodTempBandGap[1]));
    Serial.println();

    // Close the file
    file.close();
}

bool startEthernet()
{
    enable("eth");
    Serial.println("enter start Ethernet");
    Ethernet.init(pinsOut[0]);
    auto leaseStartTime = millis();
    ApplicationMonitor.IAmAlive();
    //Unfortunately we cannot wait longer for Ethernet (due to watchdog reset).
    if (Ethernet.begin(mac, 4000) == 0)
    {
        auto leaseEndTime = millis();
        ApplicationMonitor.IAmAlive();
        ApplicationMonitor.IAmAlive();
        Serial.println("Failed to configure Ethernet using DHCP after " + String(leaseEndTime - leaseStartTime) + " ms");

        // Check if Ethernet hardware present
        if (Ethernet.hardwareStatus() == EthernetNoHardware)
        {
            Serial.println("Could not detect Ethernet shield");
        }
        else
        { //Hardware OK. Try report link status
            if (Ethernet.linkStatus() == LinkOFF)
            {
                Serial.println("Ethernet not connected");
            }
            else
            {
                Serial.println("Ethernet link status unknown");
            }
        }
        return true; //We might try again later...
    }
    else
    {
        auto leaseEndTime = millis();
        ApplicationMonitor.IAmAlive();
        Serial.println("Ethernet configured correctly in " + String(leaseEndTime - leaseStartTime) + " ms");
    }
    return false;
}

void intPins(int theArray[], int sizeOfArray, boolean function)
{
    if (function == false)
    {
        for (int x = 0; x < sizeOfArray; x++)
        {
            pinMode(theArray[x], INPUT);
            ApplicationMonitor.IAmAlive();
        }
    }
    else if (function == true)
    {
        for (int x = 0; x < sizeOfArray; x++)
        {
            pinMode(theArray[x], OUTPUT);
            ApplicationMonitor.IAmAlive();
        }
    }
}

/*************************
  _                       
 | |                      
 | |     ___   ___  _ __  
 | |    / _ \ / _ \| '_ \ 
 | |___| (_) | (_) | |_) |
 |______\___/ \___/| .__/ 
                   | |    
                   |_|    
*************************/

void loop()
{
    if (loopCount == 21)
    {
        printDataSerial();
        loopCount = 1;
    }

    readSensors();                   // reads temp sensors and current sensors of mixers
    errorMessage = checkForErrors(); //checks for error after sensors was read
    if (errorMessage == true)        //check if there was an error
    {
        Serial.println("Error!!!!");
        Serial.print(errorMessages[errorIndex]);
        Alarm();
        outputControl();
        displayMenuUpdate(true);
        delay(1000);

        while (errorMessage == true)
        {
            readSensors();
            errorMessage = checkForErrors();
            ApplicationMonitor.IAmAlive();

            if (timeForLogData())
            {
                pushingBoxSend("No");
                logSD("No");
            }
            if (digitalRead(pinsIn[0]) == HIGH || digitalRead(pinsIn[1]) == HIGH || digitalRead(pinsIn[2]) == HIGH || digitalRead(pinsIn[3]) == HIGH || digitalRead(pinsIn[4]) == HIGH)
            {
                buttonRecentlyPressed = millis();
                wasButtonPressed = true;
            }

            if (displayTimeOut < (millis() - buttonRecentlyPressed))
            {
                wasButtonPressed = false;
                mainDisplayed = false;
                userManLoggedIn = false;
                userOpLoggedIn = false;
            }

            if (wasButtonPressed == true)
            {
                displayMenuUpdate(false); // update display, no error
                if (mainDisplayed == false)
                {
                    tft.fillScreen(ILI9486_WHITE);
                    displayMainMenu();
                    mainDisplayed = true;
                }
            }
            if (loopCount >= 20)
            {
                if (wasButtonPressed == false)
                {
                    displayMenuUpdate(true); //update display, with error
                }
            }
        }
        pushingBoxSend("Rebot_after_error");
        logSD("Reboot_after_error");
        delay(1000);
        reboot();
    }
    if (errorMessage == false)
    {
        if (mode == 1)
        {
            Serial.println("No_error");

            if (digitalRead(pinsIn[0]) == HIGH || digitalRead(pinsIn[1]) == HIGH || digitalRead(pinsIn[2]) == HIGH || digitalRead(pinsIn[3]) == HIGH || digitalRead(pinsIn[4]) == HIGH)
            {
                buttonRecentlyPressed = millis();
                wasButtonPressed = true;
            }

            if (displayTimeOut < (millis() - buttonRecentlyPressed))
            {
                wasButtonPressed = false;
                mainDisplayed = false;
                userManLoggedIn = false;
                userOpLoggedIn = false;
            }

            if (wasButtonPressed == true)
            {
                displayMenuUpdate(false); // update display, no error
                if (mainDisplayed == false)
                {
                    tft.fillScreen(ILI9486_WHITE);
                    displayMainMenu();
                    mainDisplayed = true;
                }
            }

            if (loopCount >= 20)
            {
                if (wasButtonPressed == false)
                {
                    productionStatsDisp();
                }
            }

            tempController();
            outputControl();
            ApplicationMonitor.IAmAlive();
            timeAvalible = updateTime();

            if (timeForLogData())
            {
                pushingBoxSend("No");
                logSD("No");
            }

            ApplicationMonitor.IAmAlive();
        }
        //Flush mode
        else if (mode == 2)
        {
            if (fillerPurgeCycle())
            {
                purgeInit = true;
            }
            if (purgeInit)
            {
                if (flushtype == 1) //hot flush
                {
                    Serial.println("Enter HOT FLUSH");
                    tft.setCursor(5, 5);
                    tft.setTextColor(ILI9486_DARKCYAN);
                    tft.print("HOT FLUSH IN PROSESS");
                    Alarm(); // sets all pins to low execpt alarm pin
                    control.prod[0] = true;
                    control.prod[1] = true;
                    control.water[0] = true;
                    control.water[1] = true;

                    while (millis() - startTime < totalTime)
                    {
                        ApplicationMonitor.IAmAlive();
                        readSensors();
                        tempController();
                        outputControl();
                        delay(200);
                    }

                    if (millis() - startTime >= totalTime)
                    {
                        pushingBoxSend("Hot_Flush");
                        logSD("Hot_Flush");
                        delay(200);
                        tft.fillScreen(ILI9486_WHITE);
                        tft.setTextSize(4);
                        tft.setCursor(5, 5);
                        tft.print("Flush done!");
                        delay(2000);
                        purgeInit = false;
                        reboot();
                    }
                }
                else if (flushtype == 2) //cold flush
                {
                    Serial.println("Enter COLD FLUSH");
                    tft.setCursor(5, 5);
                    tft.setTextColor(ILI9486_DARKCYAN);
                    tft.print("COLD FLUSH IN PROSESS");
                    Alarm(); // sets all pins to low execpt alarm pin
                    control.prod[0] = true;
                    control.prod[1] = true;
                    outputControl();

                    while (millis() - startTime < totalTime)
                    {
                        ApplicationMonitor.IAmAlive();
                        delay(200);
                    }

                    if (millis() - startTime >= totalTime)
                    {
                        pushingBoxSend("Cold_Flush");
                        logSD("Cold_Flush");
                        tft.fillScreen(ILI9486_WHITE);
                        tft.setTextSize(4);
                        tft.setCursor(5, 5);
                        tft.print("Flush done!");
                        delay(2000);
                        reboot();
                    }
                }
            }
            //--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Added logic for product prime cycle
            // 14-05-2020
            else if (mode == 3)
            {
                if (fillerPurgeCycle())
                {
                    menu = 1;
                    mode = 1;
                    tft.fillScreen(ILI9486_WHITE);
                    tft.setCursor(5, 5);
                    tft.print("Product Priming Done!");
                    delay(1000);
                    displayMainMenu();
                }
            }
        }
        loopCount++;
    }
    bool fillerPurgeCycle()
    {
        if (PurgeCounter == 0)
        {
            purgeCycleStartTime = millis();
            digitalWrite(pinsOut[13], HIGH);
            PurgeCounter++;
        }
        else if (PurgeCounter == 1 && (milli() - purgeCycleStartTime >= 2000))
        {
            PurgeCounter++;
            digitalWrite(pinsOut[13], lOW);
            purgeCycleStartTime = millis();
        }
        else if (PurgeCounter == 3 && (millis() - purgeCycleStartTime >= 1000))
        {
            PurgeCounter++;
            digitalWrite(pinsOut[13], HIGH);
            purgeCycleStartTime = millis();
        }
        else if (PurgeCounter == 4 && (milli() - purgeCycleStartTime >= 2000))
        {
            PurgeCounter = 0;
            digitalWrite(pinsOut[13], lOW);
            return true;
        }

        return false;
    }
    float operatMax31865_1(void)
    {
        float temperature_1 = max_1.temperature(RNOMINAL, RREF);
        return temperature_1;
    }

    float operatMax31865_2(void)
    {
        float temperature_2 = max_2.temperature(RNOMINAL, RREF);
        return temperature_2;
    }

    void printDataSerial()
    {
        Serial.println("");
        Serial.println("PROD : Set Temp  ");
        Serial.println(String(Parameters.prodTempSetValue));
        Serial.println("PROD: Temp  ");
        Serial.println(String(sensordata.temp[0]));
        Serial.println("PROD: Mixer current Set value  ");
        Serial.println(String(Parameters.lowAlarm[0]));
        Serial.println("PROD: Mixer current value  ");
        Serial.println(String(sensordata.mixer[0]));
        Serial.println("PROD: Mixer ON or OFF  ");
        Serial.println(String(control.prod[0]));
        Serial.println("WATER: Temp  ");
        Serial.println(String(sensordata.temp[1]));
        Serial.println("WATER: Mixer current Set value  ");
        Serial.println(String(Parameters.lowAlarm[1]));
        Serial.println("WATER: Mixer current value  ");
        Serial.println(String(sensordata.mixer[1]));
        Serial.println("WATER: Mixer:  ");
        Serial.println(String(control.prod[1]));
        Serial.println("12kW heater:  ");
        Serial.println(String(control.heat[0]));
        Serial.println("24kW heater:  ");
        Serial.println(String(control.heat[1]));
        Serial.println("36kW heater:  ");
        Serial.println(String(control.heat[2]));
        Serial.println("System Ready:  ");
        Serial.println(String(control.status[0]));
        Serial.println("Alarm:  ");
        Serial.println(String(control.status[1]));
    }

    void productionStatsDisp()
    {
        enable("Disp");
        tft.fillScreen(ILI9486_WHITE);
        tft.setCursor(5, 5);
        tft.setTextColor(ILI9486_BLACK);
        tft.setTextSize(4);
        tft.print("PRODUCTION MODE:");
        tft.setTextSize(2);
        tft.setCursor(40, 50);
        tft.print("Product Temp set value:");
        tft.setCursor(380, 50);
        tft.print(String(Parameters.prodTempSetValue));
        tft.setCursor(40, 75);
        tft.print("Product temperature:");
        tft.setCursor(380, 75);
        tft.print(String(sensordata.temp[0]));
        tft.setCursor(40, 100);
        tft.print("Water temperature:");
        tft.setCursor(380, 100);
        tft.print(String(sensordata.temp[1]));
        tft.setCursor(40, 125);
        tft.print("Heater 12kW status:");
        tft.setCursor(40, 150);
        tft.print("Heater 24kW status:");
        tft.setCursor(40, 175);
        tft.print("Heater 36kW status:");
        if (control.heat[0] == false)
        {
            tft.setCursor(380, 125);
            tft.print("OFF");
        }
        else if (control.heat[0] == true)
        {
            tft.setCursor(380, 125);
            tft.print("ON");
        }
        if (control.heat[1] == false)
        {
            tft.setCursor(380, 150);
            tft.print("OFF");
        }
        else if (control.heat[1] == true)
        {
            tft.setCursor(380, 150);
            tft.print("ON");
        }
        if (control.heat[2] == false)
        {
            tft.setCursor(380, 175);
            tft.print("OFF");
        }
        else if (control.heat[2] == true)
        {
            tft.setCursor(380, 175);
            tft.print("ON");
        }
        if (control.status[0] == false)
        {
            tft.setCursor(40, 200);
            tft.setTextColor(ILI9486_RED);
            tft.print("System ready");
            tft.setCursor(400, 200);
            tft.print("NO");
        }
        else if (control.status[0] == true)
        {
            tft.setCursor(40, 200);
            tft.setTextColor(ILI9486_GREEN);
            tft.print("System ready");
            tft.setCursor(400, 200);
            tft.print("YES");
        }
        tft.setTextColor(ILI9486_DARKCYAN);
        tft.setCursor(40, 300);
        tft.print("Press any button to enter menu");
        return;
    }

    void outputControl()
    {
        ApplicationMonitor.IAmAlive();
        if (control.heat[0] == true)
        {
            digitalWrite(pinsOut[4], HIGH);
        }
        if (control.heat[1] == true)
        {
            digitalWrite(pinsOut[5], HIGH);
        }
        if (control.heat[2] == true)
        {
            digitalWrite(pinsOut[6], HIGH);
        }
        if (control.heat[0] == false)
        {
            digitalWrite(pinsOut[4], LOW);
        }
        if (control.heat[1] == false)
        {
            digitalWrite(pinsOut[5], LOW);
        }
        if (control.heat[2] == false)
        {
            digitalWrite(pinsOut[6], LOW);
        }
        if (control.prod[0] == true)
        {
            digitalWrite(pinsOut[7], HIGH);
        }
        if (control.prod[1] == true)
        {
            digitalWrite(pinsOut[8], HIGH);
        }
        if (control.water[0] == true)
        {
            digitalWrite(pinsOut[9], HIGH);
        }
        if (control.water[1] == true)
        {
            digitalWrite(pinsOut[10], HIGH);
        }
        if (control.prod[0] == false)
        {
            digitalWrite(pinsOut[7], LOW);
        }
        if (control.prod[1] == false)
        {
            digitalWrite(pinsOut[8], LOW);
        }
        if (control.water[0] == false)
        {
            digitalWrite(pinsOut[9], LOW);
        }
        if (control.water[1] == false)
        {
            digitalWrite(pinsOut[10], LOW);
        }
        if (control.status[0] == true)
        {
            digitalWrite(pinsOut[11], HIGH);
        }
        if (control.status[1] == true)
        {
            digitalWrite(pinsOut[12], HIGH);
        }
        if (control.status[0] == false)
        {
            digitalWrite(pinsOut[11], LOW);
        }
        if (control.status[1] == false)
        {
            digitalWrite(pinsOut[12], LOW);
        }
    }

    void readSensors()
    {
        Serial.println("Reading sensors");
        enable("tempProd");
        sensordata.temp[0] = operatMax31865_1();
        enable("tempWater");
        sensordata.temp[1] = operatMax31865_2();
        sensordata.mixer[0] = analogRead(A8) / 102.4;
        sensordata.mixer[1] = analogRead(A9) / 102.4;
        ApplicationMonitor.IAmAlive();
    }

    void tempController()
    {
        if (millis() - tempConUpdate >= tempConDelay)
        {
            //control 24kW element
            if (sensordata.temp[0] < (Parameters.prodTempSetValue - Parameters.heaterStop[0]))
            {
                control.heat[1] = true;
            }
            if (sensordata.temp[0] > (Parameters.prodTempSetValue - Parameters.heaterStop[0]))
            {
                control.heat[1] = false;
            }
            //control 36kW element
            if (sensordata.temp[0] < (Parameters.prodTempSetValue - Parameters.heaterStop[1]))
            {
                control.heat[2] = true;
            }
            if (sensordata.temp[0] > (Parameters.prodTempSetValue - Parameters.heaterStop[1]))
            {
                control.heat[2] = false;
            }
            //control 12kW heater
            if (sensordata.temp[0] < (Parameters.prodTempSetValue + (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1])))
            {
                control.heat[0] = true;
            }
            if (sensordata.temp[0] > (Parameters.prodTempSetValue + (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1])))
            {
                control.heat[0] = false;
            }
            tempConUpdate = millis();
        }
        //system ready
        if (sensordata.temp[0] < (Parameters.prodTempSetValue + (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1])) && sensordata.temp[0] > (Parameters.prodTempSetValue - (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1])))
        {
            control.status[0] = true;
        }
        if (!(sensordata.temp[0] < (Parameters.prodTempSetValue + (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1])) && sensordata.temp[0] > (Parameters.prodTempSetValue - (Parameters.prodTempBandGap[0] + Parameters.prodTempBandGap[1]))))
        {
            control.status[0] = false;
        }
    }

    bool checkForErrors()
    {
        Serial.println("Checking for errors");
        ApplicationMonitor.IAmAlive();
        if (sensordata.mixer[0] <= Parameters.lowAlarm[0])
        {
            Alarm();
            errorIndex = 2;
            return true;
        }
        else if (sensordata.mixer[1] <= Parameters.lowAlarm[1])
        {
            Alarm();
            errorIndex = 3;
            return true;
        }
        else if (sensordata.temp[1] >= Parameters.waterOverTemp)
        {
            Alarm();
            errorIndex = 4;
            return true;
        }

        return false;
    }

    void Alarm()
    {
        control.status[0] = false;
        control.status[1] = true;
        control.heat[0] = false;
        control.heat[1] = false;
        control.heat[2] = false;
        control.prod[0] = false;
        control.prod[1] = false;
        control.water[0] = false;
        control.water[1] = false;
    }

    void saveConfiguration(const char *filename, const parameters &ParametersIn)
    {
        // Delete existing file, otherwise the configuration is appended to the file
        SD.remove(filename);

        // Open file for writing
        File file = SD.open(filename, FILE_WRITE);
        if (!file)
        {
            Serial.println(F("Failed to create file"));
            return;
        }

        // Allocate the memory pool on the stack
        StaticJsonBuffer<256> jsonBuffer;

        // Parse the root object
        JsonObject &root = jsonBuffer.createObject();

        // Set the values
        root["prodPumpFlushDelay"] = ParametersTemp.prodFlushPumpDelay;
        root["prodTempSetValue"] = ParametersTemp.prodTempSetValue;
        root["waterOverTemp"] = ParametersTemp.waterOverTemp;
        root["prodTempHysteresis"] = ParametersTemp.prodTempHysteresis;
        root["heat24stop"] = ParametersTemp.heaterStop[0];
        root["heat36stop"] = ParametersTemp.heaterStop[1];
        root["prodLowAlarm"] = ParametersTemp.lowAlarm[0];
        root["waterLowAlarm"] = ParametersTemp.lowAlarm[1];
        root["prodBandGap_width"] = ParametersTemp.prodTempBandGap[0];
        root["prodBandGap_offset"] = ParametersTemp.prodTempBandGap[1];

        // Serialize JSON to file
        if (root.printTo(file) == 0)
        {
            Serial.println(F("Failed to write to file"));
        }

        // Close the file
        file.close();
    }

    void logSD(const char *execption)
    {
        enable("SD");
        String dataString;
        dataString += timeClient_.getFormattedTime() + ",";
        if (execption == "No")
        {
            if (errorMessage == false)
            {
                dataString += "No_Error,";
            }
            else if (errorMessage == true)
            {
                dataString += String(errorMessages[errorIndex]) + ",";
            }
        }
        else if (execption != "No")
        {
            dataString += String(execption) + ",";
        }
        dataString += String(sensordata.temp[0]) + ",";
        dataString += String(sensordata.temp[1]) + ",";
        dataString += String(Parameters.prodTempSetValue) + ",";
        dataString += String(control.heat[0]) + ",";
        dataString += String(control.heat[1]) + ",";
        dataString += String(control.heat[2]) + ",";
        dataString += String(control.status[0]);

        File file = SD.open("dataLog.txt", FILE_WRITE);

        if (!file)
        {
            Serial.println(F("Failed to create file"));
            return;
        }

        if (file)
        {
            file.println(dataString);
            file.close();
            // print to the serial port too:
            Serial.println(dataString);
        }
        else
        {
            Serial.println("error opening datalog.txt");
        }
    }

    bool prepareForEthernetComms()
    {
        ApplicationMonitor.IAmAlive();
        if (ethFailed)
        {
            if (ethernetInitRetryIsDue())
            {
                ethFailed = startEthernet();
                ApplicationMonitor.IAmAlive();
            }
            else
            {
                Serial.println("Ethernet not initialized: Retry not due");
            }
            // Else ethernet remains unitialized until timeout before retry.
        }
        if (!ethFailed)
        {
            const auto linkStatus = Ethernet.linkStatus();
            if (linkStatus != LinkON)
            {
                Serial.println("Cable not connected");
                ApplicationMonitor.IAmAlive();
            }
            else
            {
                Ethernet.maintain();
            }
            return linkStatus == LinkON;
        }
        ApplicationMonitor.IAmAlive();
        return false;
    }

    bool ethernetInitRetryIsDue()
    {
        static Deadline deadline(Duration::minutes(5));
        return deadline.resetIfDue();
        ApplicationMonitor.IAmAlive();
    }

    void reboot()
    {
        wdt_disable();
        wdt_enable(WDTO_15MS);
        while (1)
        {
        }
    }

    bool updateTime()
    {
        if (prepareForEthernetComms())
        {
            timeClient_.update();
            ApplicationMonitor.IAmAlive();
            return true;
        }
        return false;
    }

    bool timeForLogData()
    {
        static Deadline publish(Duration::minutes(2));
        static bool firstPublishDone = false;
        if (publish.isDue() || !firstPublishDone)
        {
            firstPublishDone = true;
            publish.reset();
            return true;
        }
        return false;
    }

    bool pushingBoxSend(const char *execption)
    {
        ApplicationMonitor.IAmAlive();
        if (prepareForEthernetComms())
        {
            ApplicationMonitor.IAmAlive();
            EthernetClient client;
            Serial.println("Sending GET request to pushingBox");
            if (!client.connect(pushingBoxHost, httpPort))
            {
                Serial.println("connection to pushingbox failed");
                return false;
            }
            ApplicationMonitor.IAmAlive();
            String url = "/pushingbox?";
            url += "devid=";
            url += devid;
            if (execption == "No")
            {
                if (errorMessage == false)
                {
                    url += "&errorLog=\"No_Error\"";
                }
                else if (errorMessage == true)
                {
                    url += "&errorLog=\"" + String(errorMessages[errorIndex]) + "\"";
                }
            }
            else if (execption != "No")
            {
                url += "&errorLog=\"" + String(execption) + "\"";
            }
            url += "&prodTemp=" + String(sensordata.temp[0]);
            url += "&waterTemp=" + String(sensordata.temp[1]);
            url += "&prodSetValue=" + String(Parameters.prodTempSetValue);
            url += "&heat12=" + String(control.heat[0]);
            url += "&heat24=" + String(control.heat[1]);
            url += "&heat36=" + String(control.heat[2]);
            url += "&sysReady=" + String(control.status[0]);
            url += "&prodMixVal=" + String(sensordata.mixer[0]);
            url += "&waterMixVal=" + String(sensordata.mixer[1]);
            client.print("GET " + url);
            client.println(" HTTP/1.1");
            client.print("Host: ");
            client.println(pushingBoxHost);
            client.println("User-Agent: Arduino");
            client.println();
            client.println();
            ApplicationMonitor.IAmAlive();
            unsigned long timeout = millis();

            while (client.available() == 0)
            {
                ApplicationMonitor.IAmAlive();
                if (millis() - timeout > 4000)
                {

                    Serial.println(">>> Client Timeout !");
                    client.stop();
                    return false;
                }
            }

            while (client.available())
            {
                String line = client.readStringUntil('\r');
                Serial.println(line);
            }

            Serial.println("Data Sent!");
            Serial.println();
            return true;
        }
    }

    void displayError(short errorIndex2)
    {
        tft.setCursor(5, 5);
        tft.setTextColor(ILI9486_RED);
        tft.setTextSize(5);
        tft.print("Error:");
        tft.setTextSize(2);
        tft.setCursor(40, 50);
        String error = errorMessages[errorIndex2];
        error.replace("_", " ");
        tft.print(error);
    }

    void displayMenuUpdate(boolean error)
    {
        Serial.println();
        Serial.println("Enter display menu update");
        enable("Disp");
        if (error == true)
        {
            tft.fillScreen(ILI9486_WHITE);
            displayError(errorIndex);
        }
        if (error == false)
        {
            if (menu == 1) //Main menu
            {
                ApplicationMonitor.IAmAlive();
                if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                {
                    if (menuItemMain == 1)
                    {
                        return;
                    }
                    else if (menuItemMain > 1)
                    {
                        menuItemMain--;
                        Serial.println("up was pressed");
                        tft.fillScreen(ILI9486_WHITE);
                        displayMainMenu();
                    }
                }
                if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                {
                    if (menuItemMain == 4)
                    {
                        return;
                    }
                    else if (menuItemMain < 4)
                    {
                        menuItemMain++;
                        Serial.println("down was pressed");
                        tft.fillScreen(ILI9486_WHITE);
                        displayMainMenu();
                    }
                }
                if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                {
                    if (menuItemMain == 1)
                    {
                        menu = 2;
                        menuItemMain = 1;
                        tft.fillScreen(ILI9486_WHITE);
                        if (userOpLoggedIn == true)
                        {
                            displayMenuSetupOp();
                        }
                        else if (userOpLoggedIn == false)
                        {
                            userLogin();
                        }
                    }
                    else if (menuItemMain == 2)
                    {
                        menu = 3;
                        flushtype = 1;
                        menuItemMain = 1;
                        tft.fillScreen(ILI9486_WHITE);
                        displayFlushMenu();
                    }
                    else if (menuItemMain == 3)
                    {
                        menu = 4;
                        menuItemMain = 1;
                        tft.fillScreen(ILI9486_WHITE);
                        if (userManLoggedIn == true)
                        {
                            displayMenuSetupMan();
                        }
                        else if (userManLoggedIn == false)
                        {
                            userLogin();
                        }
                    }
                    else if (menuItemMain == 4)
                    {
                        pushingBoxSend("Manual_Reset");
                        logSD("Manual_Reset");
                        tft.fillScreen(ILI9486_WHITE);
                        reboot();
                    }
                    else
                        //--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                        // Jason Waugh
                        // Added logic for selection of product prime mode
                        // 14-05-2020
                        if (menuItemMain == 5)
                    {
                        menu = 5; // Outside of used case range for input monitoring. This ensures no user inputs will effect the priming cycle
                        mode = 3; // added mode for product priming cycle
                        PurgeCounter = 0;
                        tft.fillScreen(ILI9486_WHITE);
                        menuItemMain = 1;
                        tft.setCursor(50, 50);
                        tft.print("PRODUCT PRIMING");
                    }
                    //--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                }
            }
            else if (menu == 2) //Program mode
            {
                if (userOpLoggedIn == true) //checks if opperator is loged in
                {
                    ApplicationMonitor.IAmAlive();
                    if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                    {
                        menuItemSetupOp--;
                        tft.fillScreen(ILI9486_WHITE);
                        Serial.println("up was pressed");
                        displayMenuSetupOp();
                    }
                    if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                    {
                        menuItemSetupOp++;
                        tft.fillScreen(ILI9486_WHITE);
                        Serial.println("down was pressed");
                        displayMenuSetupOp();
                    }
                    if (digitalRead(pinsIn[1]) == HIGH) //check if left button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        switch (menuItemSetupOp)
                        {
                        case 1:
                            if (ParametersTemp.prodTempSetValue == 80)
                            {
                                displayMenuSetupOp();
                                return;
                            }
                            else if (ParametersTemp.prodTempSetValue > 80)
                            {
                                ParametersTemp.prodTempSetValue--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupOp();
                            }
                            break;
                        case 2:
                            return;
                            break;
                        case 3:
                            return;
                            break;
                        }
                    }
                    if (digitalRead(pinsIn[2]) == HIGH) //check if right button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        switch (menuItemSetupOp)
                        {
                        case 1:
                            if (ParametersTemp.prodTempSetValue == 85)
                            {
                                displayMenuSetupOp();
                                return;
                            }
                            else if (ParametersTemp.prodTempSetValue < 85)
                            {
                                ParametersTemp.prodTempSetValue++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupOp();
                            }
                            break;

                        case 2:
                            return;
                            break;
                        case 3:
                            return;
                            break;
                        }
                    }
                    if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (menuItemSetupOp == 2)
                        {
                            menu = 1;
                            saveConfiguration(fileConfig, ParametersTemp);
                            delay(200);
                            pushingBoxSend("Manual_Reset");
                            logSD("Manual_Reset");
                            delay(2000);
                            reboot();
                        }
                        else if (menuItemSetupOp == 3)
                        {
                            menu = 1;
                            userOpLoggedIn = false;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMainMenu();
                        }
                    }
                }
                else if (userOpLoggedIn == false)
                {
                    ApplicationMonitor.IAmAlive();
                    if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                    {
                        Serial.println("up was pressed");
                        if (passIndex == 1)
                        {
                            if (tempPass1 == 9)
                            {
                                return;
                            }
                            else if (tempPass1 < 9)
                            {
                                tempPass1++;
                            }
                        }
                        else if (passIndex == 2)
                        {
                            if (tempPass2 == 9)
                            {
                                return;
                            }
                            else if (tempPass2 < 9)
                            {
                                tempPass2++;
                            }
                        }
                        else if (passIndex == 3)
                        {
                            if (tempPass3 == 9)
                            {
                                return;
                            }
                            else if (tempPass3 < 9)
                            {
                                tempPass3++;
                            }
                        }
                        else if (passIndex == 4)
                        {
                            if (tempPass4 == 9)
                            {
                                return;
                            }
                            else if (tempPass4 < 9)
                            {
                                tempPass4++;
                            }
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        Serial.println("down was pressed");
                        if (passIndex == 1)
                        {
                            if (tempPass1 == 0)
                            {
                                return;
                            }
                            else if (tempPass1 > 0)
                            {
                                tempPass1--;
                            }
                        }
                        else if (passIndex == 2)
                        {
                            if (tempPass2 == 0)
                            {
                                return;
                            }
                            else if (tempPass2 > 0)
                            {
                                tempPass2--;
                            }
                        }
                        else if (passIndex == 3)
                        {
                            if (tempPass3 == 0)
                            {
                                return;
                            }
                            else if (tempPass3 > 0)
                            {
                                tempPass3--;
                            }
                        }
                        else if (passIndex == 4)
                        {
                            if (tempPass4 == 0)
                            {
                                return;
                            }
                            else if (tempPass4 > 0)
                            {
                                tempPass4--;
                            }
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[1]) == HIGH) //check if left button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (passIndex == 1)
                        {
                            return;
                        }
                        else if (passIndex > 1)
                        {
                            passIndex--;
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[2]) == HIGH) //check if right button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (passIndex == 5)
                        {
                            return;
                        }
                        else if (passIndex < 5)
                        {
                            passIndex++;
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                    {
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                        ApplicationMonitor.IAmAlive();

                        if (passIndex == 5)
                        {
                            menu = 1;
                            userManLoggedIn = false;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMainMenu();
                        }
                        else if (tempPass1 == 7 && tempPass2 == 3 && tempPass3 == 8 && tempPass4 == 2) //passcode = 7382
                        {
                            userOpLoggedIn = true;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMenuSetupOp();
                            passIndex = 1;
                            tempPass1 = 0;
                            tempPass2 = 0;
                            tempPass3 = 0;
                            tempPass4 = 0;
                            displayMenuUpdate(false);
                        }
                        else
                        {
                            tft.setTextSize(2);
                            tft.setCursor(20, 220);
                            tft.setTextColor(ILI9486_RED);
                            tft.println("Incorect, try again");
                        }
                    }
                }
            }
            else if (menu == 3)
            {
                ApplicationMonitor.IAmAlive();
                if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                {
                    flushtype--;
                    Serial.println("up was pressed");
                    tft.fillScreen(ILI9486_WHITE);
                    displayFlushMenu();
                }
                if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                {
                    flushtype++;
                    Serial.println("down was pressed");
                    tft.fillScreen(ILI9486_WHITE);
                    displayFlushMenu();
                }
                if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                {
                    if (flushtype == 1)
                    {
                        menu = 3;
                        mode = 2;
                        tft.fillScreen(ILI9486_WHITE);
                        startTime = millis();
                        return;
                    }
                    else if (flushtype == 2)
                    {
                        menu = 3;
                        mode = 2;
                        tft.fillScreen(ILI9486_WHITE);
                        startTime = millis();
                        return;
                    }
                    else if (flushtype == 3)
                    {
                        menu = 1;
                        mode = 1;
                        tft.fillScreen(ILI9486_WHITE);
                        displayMainMenu();
                    }
                }
            }
            else if (menu == 4)
            {
                if (userManLoggedIn == true) //checks if opperator is loged in
                {
                    ApplicationMonitor.IAmAlive();
                    if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                    {
                        menuItemSetupMan--;
                        tft.fillScreen(ILI9486_WHITE);
                        Serial.println("up was pressed");
                        displayMenuSetupMan();
                    }
                    if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                    {
                        menuItemSetupMan++;
                        tft.fillScreen(ILI9486_WHITE);
                        Serial.println("down was pressed");
                        displayMenuSetupMan();
                    }
                    if (digitalRead(pinsIn[1]) == HIGH) //check if left button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        switch (menuItemSetupMan)
                        {
                        case 1:
                            if (ParametersTemp.prodFlushPumpDelay == 2)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodFlushPumpDelay > 2)
                            {
                                ParametersTemp.prodFlushPumpDelay--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 2:
                            if (ParametersTemp.heaterStop[0] == 5)
                            {
                                return;
                            }
                            else if (ParametersTemp.heaterStop[0] > 5)
                            {
                                ParametersTemp.heaterStop[0]--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 3:
                            if (ParametersTemp.heaterStop[1] == 15)
                            {
                                return;
                            }
                            else if (ParametersTemp.heaterStop[0] > 15)
                            {
                                ParametersTemp.heaterStop[1]--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 4:
                            if (ParametersTemp.waterOverTemp == 90)
                            {
                                return;
                            }
                            else if (ParametersTemp.waterOverTemp > 90)
                            {
                                ParametersTemp.waterOverTemp--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 5:
                            if (ParametersTemp.lowAlarm[1] == 1)
                            {
                                return;
                            }
                            else if (ParametersTemp.lowAlarm[1] > 1)
                            {
                                ParametersTemp.lowAlarm[1]--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 6:
                            if (ParametersTemp.lowAlarm[0] == 1)
                            {
                                return;
                            }
                            else if (ParametersTemp.lowAlarm[0] > 1)
                            {
                                ParametersTemp.lowAlarm[0]--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 7:
                            if (ParametersTemp.prodTempHysteresis == 5)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempHysteresis > 5)
                            {
                                ParametersTemp.prodTempHysteresis - 5;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 8:
                            if (ParametersTemp.prodTempBandGap[0] == 1)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempBandGap[0] > 1)
                            {
                                ParametersTemp.prodTempBandGap[0] - 0.1;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 9:
                            if (ParametersTemp.prodTempBandGap[1] == -2)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempBandGap[1] > -2)
                            {
                                ParametersTemp.prodTempBandGap[1]--;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 10:
                            return;
                            break;
                        case 11:
                            return;
                            break;
                        }
                    }
                    if (digitalRead(pinsIn[2]) == HIGH) //check if right button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        switch (menuItemSetupMan)
                        {
                        case 1:
                            if (ParametersTemp.prodFlushPumpDelay == 60)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodFlushPumpDelay < 60)
                            {
                                ParametersTemp.prodFlushPumpDelay++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 2:
                            if (ParametersTemp.heaterStop[0] == 20)
                            {
                                return;
                            }
                            else if (ParametersTemp.heaterStop[0] < 20)
                            {
                                ParametersTemp.heaterStop[0]++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 3:
                            if (ParametersTemp.heaterStop[1] == 30)
                            {
                                return;
                            }
                            else if (ParametersTemp.heaterStop[0] < 30)
                            {
                                ParametersTemp.heaterStop[1]++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 4:
                            if (ParametersTemp.waterOverTemp == 95)
                            {
                                return;
                            }
                            else if (ParametersTemp.waterOverTemp < 95)
                            {
                                ParametersTemp.waterOverTemp++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 5:
                            if (ParametersTemp.lowAlarm[1] == 10)
                            {
                                return;
                            }
                            else if (ParametersTemp.lowAlarm[1] < 10)
                            {
                                ParametersTemp.lowAlarm[1]++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 6:
                            if (ParametersTemp.lowAlarm[0] == 10)
                            {
                                return;
                            }
                            else if (ParametersTemp.lowAlarm[0] < 10)
                            {
                                ParametersTemp.lowAlarm[0]++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 7:
                            if (ParametersTemp.prodTempHysteresis == 60)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempHysteresis < 60)
                            {
                                ParametersTemp.prodTempHysteresis + 5;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 8:
                            if (ParametersTemp.prodTempBandGap[0] == 4)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempBandGap[0] < 4)
                            {
                                ParametersTemp.prodTempBandGap[0] + 0.1;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 9:
                            if (ParametersTemp.prodTempBandGap[1] == 2)
                            {
                                return;
                            }
                            else if (ParametersTemp.prodTempBandGap[1] < 2)
                            {
                                ParametersTemp.prodTempBandGap[1]++;
                                tft.fillScreen(ILI9486_WHITE);
                                displayMenuSetupMan();
                            }
                            break;
                        case 10:
                            return;
                            break;
                        case 11:
                            return;
                            break;
                        }
                    }
                    if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (menuItemSetupMan == 10)
                        {
                            menu = 1;
                            saveConfiguration(fileConfig, ParametersTemp);
                            pushingBoxSend("Manual_Reset");
                            logSD("Manual_Reset");
                            delay(2000);
                            reboot();
                        }
                        else if (menuItemSetupMan == 11)
                        {
                            menu = 1;
                            userManLoggedIn = false;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMainMenu();
                        }
                    }
                }
                else if (userManLoggedIn == false)
                {
                    ApplicationMonitor.IAmAlive();
                    if (digitalRead(pinsIn[3]) == HIGH) //check if up button was pressed
                    {
                        Serial.println("up was pressed");
                        if (passIndex == 1)
                        {
                            if (tempPass1 == 9)
                            {
                                return;
                            }
                            else if (tempPass1 < 9)
                            {
                                tempPass1++;
                            }
                        }
                        else if (passIndex == 2)
                        {
                            if (tempPass2 == 9)
                            {
                                return;
                            }
                            else if (tempPass2 < 9)
                            {
                                tempPass2++;
                            }
                        }
                        else if (passIndex == 3)
                        {
                            if (tempPass3 == 9)
                            {
                                return;
                            }
                            else if (tempPass3 < 9)
                            {
                                tempPass3++;
                            }
                        }
                        else if (passIndex == 4)
                        {
                            if (tempPass4 == 9)
                            {
                                return;
                            }
                            else if (tempPass4 < 9)
                            {
                                tempPass4++;
                            }
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[4]) == HIGH) //check if down button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        Serial.println("down was pressed");
                        if (passIndex == 1)
                        {
                            if (tempPass1 == 1)
                            {
                                return;
                            }
                            else if (tempPass1 > 1)
                            {
                                tempPass1--;
                            }
                        }
                        else if (passIndex == 2)
                        {
                            if (tempPass2 == 1)
                            {
                                return;
                            }
                            else if (tempPass2 > 1)
                            {
                                tempPass2--;
                            }
                        }
                        else if (passIndex == 3)
                        {
                            if (tempPass3 == 1)
                            {
                                return;
                            }
                            else if (tempPass3 > 1)
                            {
                                tempPass3--;
                            }
                        }
                        else if (passIndex == 4)
                        {
                            if (tempPass4 == 1)
                            {
                                return;
                            }
                            else if (tempPass4 > 1)
                            {
                                tempPass4--;
                            }
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[1]) == HIGH) //check if left button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (passIndex == 1)
                        {
                            return;
                        }
                        else if (passIndex > 1)
                        {
                            passIndex--;
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[2]) == HIGH) //check if right button was pressed
                    {
                        ApplicationMonitor.IAmAlive();
                        if (passIndex == 5)
                        {
                            return;
                        }
                        else if (passIndex < 5)
                        {
                            passIndex++;
                        }
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                    }
                    if (digitalRead(pinsIn[0]) == HIGH) //check if enter button was pressed
                    {
                        tft.fillScreen(ILI9486_WHITE);
                        userLogin();
                        ApplicationMonitor.IAmAlive();

                        if (passIndex == 5)
                        {
                            menu = 1;
                            userManLoggedIn = false;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMainMenu();
                        }
                        else if (tempPass1 == 3 && tempPass2 == 7 && tempPass3 == 4 && tempPass4 == 9) //passcode = 3749
                        {
                            userManLoggedIn = true;
                            tft.fillScreen(ILI9486_WHITE);
                            displayMenuSetupMan();
                            passIndex = 1;
                            tempPass1 = 0;
                            tempPass2 = 0;
                            tempPass3 = 0;
                            tempPass4 = 0;
                            displayMenuUpdate(false);
                        }
                        else
                        {
                            tft.setTextSize(2);
                            tft.setCursor(20, 220);
                            tft.setTextColor(ILI9486_RED);
                            tft.println("Incorect, try again");
                        }
                    }
                }
            }
        }
    }

    void enable(String Cs)
    {
        if (Cs == "eth")
        {
            digitalWrite(pinsOut[0], LOW);
            digitalWrite(pinsOut[1], HIGH);
            digitalWrite(7, LOW);
        }
        else if (Cs == "SD")
        {
            digitalWrite(pinsOut[0], HIGH);
            digitalWrite(pinsOut[1], LOW);
            digitalWrite(7, LOW);
        }
        else if (Cs == "Disp")
        {
            digitalWrite(pinsOut[0], HIGH);
            digitalWrite(pinsOut[1], HIGH);
            digitalWrite(7, HIGH);
        }
    }

    void displayMenuSetupOp()
    {
        switch (menuItemSetupOp)
        {
        case 0:
            menuItemSetupOp = 1;
            break;
        case 1:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("Setup:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print(">Product Temp set value");
            tft.setCursor(400, 50);
            tft.print(String(ParametersTemp.prodTempSetValue));
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(40, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(40, 300);
            tft.print("ABORT");
            break;
        case 2:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("Setup:");
            tft.setTextSize(2);
            tft.setCursor(40, 50);
            tft.print("Product Temp set value");
            tft.setCursor(400, 50);
            tft.print(String(ParametersTemp.prodTempSetValue));
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print(">DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(40, 300);
            tft.print("ABORT");
            break;
        case 3:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("Setup:");
            tft.setTextSize(2);
            tft.setCursor(40, 50);
            tft.print("Product Temp set value");
            tft.setCursor(400, 50);
            tft.print(String(ParametersTemp.prodTempSetValue));
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(40, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print(">ABORT");
            break;
        case 4:
            menuItemSetupOp = 3;
            break;
        }
    }

    void displayFlushMenu()
    {
        switch (flushtype)
        {
        case 0:
            flushtype = 1;
            break;
        case 1:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("FLUSH MODE:");
            tft.setTextSize(3);
            tft.setCursor(20, 50);
            tft.print(">Hot flush");
            tft.setCursor(40, 80);
            tft.print("Cold flush");
            tft.setCursor(40, 140);
            tft.print("Abort");
            break;
        case 2:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("FLUSH MODE:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("Hot flush");
            tft.setCursor(20, 80);
            tft.print(">Cold flush");
            tft.setCursor(40, 140);
            tft.print("Abort");
            break;
        case 3:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("FLUSH MODE:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("Hot flush");
            tft.setCursor(40, 80);
            tft.print("Cold flush");
            tft.setCursor(20, 140);
            tft.print(">Abort");
            break;
        case 4:
            flushtype = 3;
            break;
        }
    }

    void displayMainMenu()
    {
        switch (menuItemMain)
        {
        case 0:
            menuItemMain = 1;
            break;
        case 1:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("MENU:");
            tft.setTextSize(3);
            tft.setCursor(20, 50);
            tft.print(">PROGRAM MODE");
            tft.setCursor(40, 80);
            tft.print("PRODUCT FLUSH MODE");
            tft.setCursor(40, 110);
            tft.print("MAINTENACE MODE");
            tft.setCursor(40, 140);
            tft.print("SYSTEM RESET");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Option on main menu for PRODUCT PRIMING
            // 14-05-2020

            tft.setCursor(40, 170);
            tft.print("PRODUCT PRIME MODE");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            break;
        case 2:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("MENU:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("PROGRAM MODE");
            tft.setCursor(20, 80);
            tft.print(">PRODUCT FLUSH MODE");
            tft.setCursor(40, 110);
            tft.print("MAINTENACE MODE");
            tft.setCursor(40, 140);
            tft.print("SYSTEM RESET");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Option on main menu for PRODUCT PRIMING
            // 14-05-2020

            tft.setCursor(40, 170);
            tft.print("PRODUCT PRIME MODE");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            break;
        case 3:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("MENU:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("PROGRAM MODE");
            tft.setCursor(40, 80);
            tft.print("PRODUCT FLUSH MODE");
            tft.setCursor(20, 110);
            tft.print(">MAINTENACE MODE");
            tft.setCursor(40, 140);
            tft.print("SYSTEM RESET");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Option on main menu for PRODUCT PRIMING
            // 14-05-2020

            tft.setCursor(40, 170);
            tft.print("PRODUCT PRIME MODE");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            break;
        case 4:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("MENU:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("PROGRAM MODE");
            tft.setCursor(40, 80);
            tft.print("PRODUCT FLUSH MODE");
            tft.setCursor(40, 110);
            tft.print("MAINTENACE MODE");
            tft.setCursor(20, 140);
            tft.print(">SYSTEM RESET");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Option on main menu for PRODUCT PRIMING
            // 14-05-2020

            tft.setCursor(40, 170);
            tft.print("PRODUCT PRIME MODE");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            break;
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Case 5 for selection of product prime cycle
            // 14-05-2020
        case 5:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("MENU:");
            tft.setTextSize(3);
            tft.setCursor(40, 50);
            tft.print("PROGRAM MODE");
            tft.setCursor(40, 80);
            tft.print("PRODUCT FLUSH MODE");
            tft.setCursor(40, 110);
            tft.print("MAINTENACE MODE");
            tft.setCursor(20, 140);
            tft.print("SYSTEM RESET");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            // Jason Waugh
            // Adding Option on main menu for PRODUCT PRIMING
            // 14-05-2020

            tft.setCursor(40, 170);
            tft.print(">PRODUCT PRIME MODE");
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            //-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
            break;
        case 6:
            menuItemMain = 5;
            break;
        }
    }

    void displayMenuSetupMan()
    {
        switch (menuItemSetupMan)
        {
        case 0:
            menuItemSetupMan = 1;
            break;
        case 1:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 50);
            tft.print(">Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 2:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 75);
            tft.print(">24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 3:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 100);
            tft.print(">36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.setTextColor(ILI9486_BLACK);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 4:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 125);
            tft.print(">Water over heat alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.setTextColor(ILI9486_BLACK);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 5:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 150);
            tft.print(">Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 6:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 175);
            tft.print(">Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 7:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 200);
            tft.print(">Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 8:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 225);
            tft.print(">Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 9:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 250);
            tft.print(">Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_BLUE);
            tft.setCursor(20, 275);
            tft.print("DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 10:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 275);
            tft.print(">DONE");
            tft.setTextColor(ILI9486_GREEN);
            tft.setCursor(20, 300);
            tft.print("ABORT");
            break;
        case 11:
            tft.setCursor(5, 5);
            tft.setTextColor(ILI9486_BLACK);
            tft.setTextSize(5);
            tft.print("PROGRAM MODE:");
            tft.setTextSize(2);
            tft.setCursor(20, 50);
            tft.print("Product pump delay time");
            tft.setCursor(415, 50);
            tft.print(String(ParametersTemp.prodFlushPumpDelay) + "s");
            tft.setCursor(20, 75);
            tft.print("24kW heater stop Temp below SP");
            tft.setCursor(415, 75);
            tft.print(String(ParametersTemp.heaterStop[0]) + "C");
            tft.setCursor(20, 100);
            tft.print("36kW heater stop Temp below SP");
            tft.setCursor(415, 100);
            tft.print(String(ParametersTemp.heaterStop[1]) + "C");
            tft.setCursor(20, 125);
            tft.print("Water over temp alarm value");
            tft.setCursor(415, 125);
            tft.print(String(ParametersTemp.waterOverTemp) + "C");
            tft.setCursor(20, 150);
            tft.print("Water low alarm value (1-10)");
            tft.setCursor(415, 150);
            tft.print(String(ParametersTemp.lowAlarm[1]));
            tft.setCursor(20, 175);
            tft.print("Product low alarm value (1-10)");
            tft.setCursor(415, 175);
            tft.print(String(ParametersTemp.lowAlarm[0]));
            tft.setCursor(20, 200);
            tft.print("Product Temp hysteresis time");
            tft.setCursor(415, 200);
            tft.print(String(ParametersTemp.prodTempHysteresis) + "s");
            tft.setCursor(20, 225);
            tft.print("Product Temp band gap (WIDTH)");
            tft.setCursor(415, 225);
            tft.print(String(ParametersTemp.prodTempBandGap[0]) + "C");
            tft.setCursor(20, 250);
            tft.print("Product Temp band gap (OFFSET)");
            tft.setCursor(415, 250);
            tft.print(String(ParametersTemp.prodTempBandGap[1]) + "C");
            tft.setCursor(20, 275);
            tft.setTextColor(ILI9486_BLUE);
            tft.print("DONE");
            tft.setTextColor(ILI9486_RED);
            tft.setCursor(7, 300);
            tft.print(">ABORT");
            break;
        case 12:
            menuItemSetupMan = 11;
            break;
        }
    }

    void userLogin()
    {
        ApplicationMonitor.IAmAlive();
        tft.setCursor(5, 5);
        tft.setTextColor(ILI9486_BLACK);
        tft.setTextSize(3);
        tft.print("Enter your password");
        tft.setTextSize(3);
        tft.setCursor(20, 50);
        if (passIndex == 1)
        {
            tft.setTextColor(ILI9486_BLUE);
            tft.print("> " + String(tempPass1));
            tft.setTextColor(ILI9486_BLACK);
            tft.print(String(tempPass2) + String(tempPass3) + String(tempPass4));
        }
        else if (passIndex == 2)
        {
            tft.setTextColor(ILI9486_BLACK);
            tft.print("> " + String(tempPass1));
            tft.setTextColor(ILI9486_BLUE);
            tft.print(String(tempPass2));
            tft.setTextColor(ILI9486_BLACK);
            tft.print(String(tempPass3) + String(tempPass4));
        }
        else if (passIndex == 3)
        {
            tft.setTextColor(ILI9486_BLACK);
            tft.print("> " + String(tempPass1) + String(tempPass2));
            tft.setTextColor(ILI9486_BLUE);
            tft.print(String(tempPass3));
            tft.setTextColor(ILI9486_BLACK);
            tft.print(String(tempPass4));
        }
        else if (passIndex == 4)
        {
            tft.setTextColor(ILI9486_BLACK);
            tft.print("> " + String(tempPass1) + String(tempPass2) + String(tempPass3));
            tft.setTextColor(ILI9486_BLUE);
            tft.print(String(tempPass4));
        }
        if (passIndex != 5)
        {
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(40, 75);
            tft.print("ABORT");
        }
        else if (passIndex == 5)
        {
            tft.setTextColor(ILI9486_BLACK);
            tft.setCursor(40, 50);
            tft.print(String(tempPass1) + String(tempPass2) + String(tempPass3) + String(tempPass4));
            tft.setCursor(20, 75);
            tft.setTextColor(ILI9486_BLUE);
            tft.print("> ABORT");
        }
    }
