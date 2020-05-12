#include <Wire.h>
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Adafruit_MAX31865.h>

// Dynamic Variables to be adjusted in MAINTANENCE_MODE / PROGRAM_MODE

uint8_t PROD_TEMP_HYST;  // Seconds
uint8_t PROD_TEMP_BAND_WIDTH  = 1;  // degree celcius, adjustable between 1 to 4
uint8_t PROD_TEMP_BAND_OFFSET = 0;  // degree celcius, adjustable between -2 and 2
uint8_t WATER_OVER_TEMP       = 90; // degree celcius, adjustable between 90 and 95
uint8_t PROD_OVER_TEMP        = 90; // must clarify
uint8_t Bkw_HEATER_STOP       = 5;  // degree celcius, Temperature delta of PROD_TEMP and TEMP_SET for 24 KW heater turn off
uint8_t Ckw_HEATER_STOP       = 15; // degree celcius, Temperature delta of PROD_TEMP and TEMP_SET for 36 KW heater turn off

uint8_t PROD_PUMP_DELAY_TIME  = 2;  // Seconds, Time Delta between PROD_LEVEL_LOW and PROD_PUMP turn off during FLUSH_MODE


// Pump Control GPIO Declarations

#define PROD_PUMP_ON  38
#define WATER_PUMP_ON 39

// Heater Control GPIO Declarations

#define AkW_ON 34 // 12 Kw 
#define BkW_ON 35 // 24 Kw
#define CkW_ON 36 // 36 Kw

// Mixer Control GPIO Declarations

#define PROD_MIXER_ON 37

// System Light control GPIO Declarations

#define RED_LIGHT_ON      40
#define ORANGE_LIGHT_ON   41
#define GREEN_LIGHT_ON    42

// System Status control - filler output feed - GPIO Declarations

#define SYSTEM_READY_ON   43
#define FILLER_PURGE_ON   44

// Temperature Monitoring GPIO Declarations
// Temp reading done with MAX31865 and SPI interface 
//-----------------------------------------------------------------------------------------------------
// Declaration format:
// Adafruit_MAX31865::Adafruit_MAX31865(int8_t spi_cs, int8_t spi_mosi,int8_t spi_miso, int8_t spi_clk)

// Product temperature reading declarations

#define PROD_TEMP_CS  8

// Water temperature reading declarations

#define WATER_TEMP_CS 9

//SPI interface Delcarations

#define SPI_SDI 11
#define SPI_SDO 12
#define SPI_CLK 13
//-----------------------------------------------------------------------------------------------------


// Initializing MAX31865 Objects for Temperature readings using MAX311865 Library

Adafruit_MAX31865 PROD_THERMO   = Adafruit_MAX31865(PROD_TEMP_CS, SPI_SDI, SPI_SDO, SPI_CLK);
Adafruit_MAX31865 WATER_THERMO  = Adafruit_MAX31865(WATER_TEMP_CS, SPI_SDI, SPI_SDO, SPI_CLK);

// PT100 Constants declaration

#define RREF      430.0
#define RNOMINAL  100.0

// Button Interface GPIO Declarations

#define BUTTON_ENTER  29
#define BUTTON_DOWN   30
#define BUTTON_UP     31
#define BUTTON_RIGHT  32
#define BUTTON_LEFT   33

// LCD Display Declarations

// Ethernet Shield Declarations




// Temperature Variable declarations

uint8_t PROD_TEMP;
uint8_t TEMP_SET;
uint8_t WATER_TEMP;

// Level Monitoring GPIO declarations

#define WATER_LEVEL_LOW 22
#define PROD_LEVEL_LOW  23
#define PROD_TOP_UP     24

// State machine enumeration for operational mode identification

enum OPERATION_MODE{
  PRODUCTION_MODE,
  PRODUCT_FLUSH_MODE,
  PRODUCT_PRIME_MODE,
  PROGRAM_MODE,
  MAINTENENCE_MODE,
  RESET,
  WAIT
};

// System Light Tower Status mode enumerations

enum SIGNAL_TOWER_LIGHT_STATUS{
  OK,
  WATER_TANK_LEVEL_LOW,
  PRODUCT_TOP_UP,
  PROD_TEMP_UNDER,
  PROD_TEMP_OVER,
  PROD_LOW
};

// enumeration of flush type 

enum FLUSH_TYPE{
  HOT,
  COLD
};

// declaration of Flush type 

FLUSH_TYPE FLUSH_TEMP;

// Declaration of Light Tower Type

SIGNAL_TOWER_LIGHT_STATUS LIGHT_STATUS;

// Declaration of Operation mode type

OPERATION_MODE CURRENT_MODE;

// Declaration of Timer1 variables for Timer1 Interrupts 

const uint16_t T1_LOAD             = 0;       // Initial load value
const uint16_t T1_COMPARE          = 31250;   // Reload value

// Counter declaration for product filler purging cycle counters

uint8_t PRODUCT_PRIME_MODE_COUNTER = 0;       
uint8_t FILLER_PURGE_CYCLE;

uint8_t PRODUCT_FLUSH_COUNTER=0;



void setup(){

  // Initialize all Digital Pins and Set defualt States
  
  GPIO_Init();

  // Initialize Temp Sensor Objects
  
  MAX31865_Init(PROD_THERMO); // returns true for fault found
  MAX31865_Init(WATER_THERMO);
  
  // Read Initial Temperature Values
  
  PROD_TEMP  = Get_Temp(PROD_THERMO);
  WATER_TEMP = Get_Temp(WATER_THERMO);
  
  // Set Defualt State / Operation Mode
  
  Set_Mode(PRODUCTION_MODE);

  // Timer interrupt init
  
  Timer1_Init();
}
void loop(){
    Run_Mode(CURRENT_MODE);
}


// Function to run all operation mode specific logic on System peripherals

void Run_Mode(OPERATION_MODE Mode){
    // continuously Update values to work with the latest temps
    PROD_TEMP  = Get_Temp(PROD_THERMO);
    WATER_TEMP = Get_Temp(WATER_THERMO);
    
    switch(Mode){
      
        case PRODUCTION_MODE:
        
        // Check either levels too low before turning on Heater Logic and pump systems
        if(digitalRead(PROD_TOP_UP)){LIGHT_STATUS = PRODUCT_TOP_UP;}

        if((!digitalRead(PROD_LEVEL_LOW)) && (!digitalRead(WATER_LEVEL_LOW))){

            Heater_Routine(true);
            
        }else  // Water Levels too low, turn off all heaters and water pump. Continue production as normal
          if((!digitalRead(PROD_LEVEL_LOW)) && (digitalRead(WATER_LEVEL_LOW))){
          
            Heater_Routine(false);
            if(digitalRead(WATER_PUMP_ON)){digitalWrite(WATER_PUMP_ON,LOW);}
            LIGHT_STATUS = WATER_TANK_LEVEL_LOW;


        }else // Product Level too low. Heaters stay on, Product pump off and system ready goes low 
          if ((digitalRead(PROD_LEVEL_LOW))){
            Heater_Routine(true);
            LIGHT_STATUS=PROD_LOW;
            if(digitalRead(PROD_PUMP_ON)){digitalWrite(PROD_PUMP_ON,LOW);}
            digitalWrite(SYSTEM_READY_ON,LOW);
        }
        break;
        
        case PRODUCT_PRIME_MODE:
        if(FILLER_PURGE_CYCLE > 1){
            // revert back to LCD menu await inputs
        }
        break;
        case PRODUCT_FLUSH_MODE:
        // If Cold flush mode, product tank empty and delay time elapsed
        
        if(FLUSH_TEMP==COLD){
            //turn off heaters and water pumps
            
            Heater_Routine(false);
            if(digitalRead(WATER_PUMP_ON)){digitalWrite(WATER_PUMP_ON,LOW);}
            
        }else 
        //if warm flush mode, product tank empty and delay time elapsed 
        
        if(FLUSH_TEMP==HOT && !digitalRead(PROD_LOW)){
            //turn off all heaters and pumps 
            
            Heater_Routine(true);
            if(!digitalRead(WATER_PUMP_ON)){digitalWrite(WATER_PUMP_ON,HIGH);}
        }
        if(digitalRead(PROD_LEVEL_LOW)){
            if(digitalRead(PROD_MIXER_ON)){digitalWrite(PROD_MIXER_ON,LOW);}
            TCNT1 = T1_LOAD;
        }

        break;
        case PROGRAM_MODE:
        break;
        case MAINTENENCE_MODE:
        break;
    }
}
void Heater_Routine(bool Safe){
    if(Safe){
        if((PROD_TEMP < TEMP_SET)&& (WATER_TEMP < WATER_OVER_TEMP)){

                // While Prod_Temp < Set_Temp 12Kw heater stays on
                
                if((!digitalRead(AkW_ON))){digitalWrite(AkW_ON,HIGH);}

                // Check for temp delta between prod_temp and Set_Temp for individual heater turn ON
                
                if((PROD_TEMP < (TEMP_SET - Bkw_HEATER_STOP)) && (!digitalRead(BkW_ON))) {digitalWrite(BkW_ON,HIGH);}
                if((PROD_TEMP < (TEMP_SET - Ckw_HEATER_STOP)) && (!digitalRead(CkW_ON))) {digitalWrite(CkW_ON,HIGH);}

                // Check for temp delta between prod_temp and Set_Temp for individual heater turn OFF

                if((PROD_TEMP >= (TEMP_SET - Bkw_HEATER_STOP)) && (digitalRead(BkW_ON))) {digitalWrite(BkW_ON,LOW);}
                if((PROD_TEMP >= (TEMP_SET - Ckw_HEATER_STOP)) && (digitalRead(CkW_ON))) {digitalWrite(CkW_ON,LOW);}
                
                if(LIGHT_STATUS!= PROD_LOW){LIGHT_STATUS = PROD_TEMP_UNDER;}
                
                
            }else // Water temp too high, turn off all heaters, continue production as normal 
                if((PROD_TEMP < TEMP_SET)&& (WATER_TEMP >= WATER_OVER_TEMP)){
                  
                  if(digitalRead(AkW_ON)){digitalWrite(AkW_ON,LOW);}
                  if(digitalRead(BkW_ON)){digitalWrite(BkW_ON,LOW);}
                  if(digitalRead(CkW_ON)){digitalWrite(CkW_ON,LOW);} 
                   

      
            }else // Product reached temperature, turn of final heater: Still need to add Hysteresis dynamics
            
              if(PROD_TEMP>=TEMP_SET){
                // Turn off Final Heater
                if(digitalRead(AkW_ON)){digitalWrite(AkW_ON,LOW);}

                // Turn On Mixer and Pumps for production process and SYSTEM READY on 
                
                if(!digitalRead(WATER_PUMP_ON)){digitalWrite(WATER_PUMP_ON,HIGH);}

                if(!digitalRead(PROD_PUMP_ON)){digitalWrite(PROD_PUMP_ON,HIGH);}
    
                if(!digitalRead(PROD_MIXER_ON)){digitalWrite(PROD_MIXER_ON,HIGH);}
                
                LIGHT_STATUS = OK;

            }else // Product Temp too high, turn off all heaters and update system status 
              if((PROD_TEMP >= PROD_OVER_TEMP)){
                
                  if(digitalRead(AkW_ON)){digitalWrite(AkW_ON,LOW);}
                  if(digitalRead(BkW_ON)){digitalWrite(BkW_ON,LOW);}
                  if(digitalRead(CkW_ON)){digitalWrite(CkW_ON,LOW);}
                  
                  digitalWrite(SYSTEM_READY_ON,LOW);
                  LIGHT_STATUS = PROD_TEMP_OVER;
            }
    }else{
            if(digitalRead(AkW_ON)){digitalWrite(AkW_ON,LOW);}
            if(digitalRead(BkW_ON)){digitalWrite(BkW_ON,LOW);}
            if(digitalRead(CkW_ON)){digitalWrite(CkW_ON,LOW);}
    }
}

// Read Temperature Sensors 
uint8_t Get_Temp(Adafruit_MAX31865 Thermo){
    return Thermo.temperature(RNOMINAL,RREF);
}

// Set Current mode type to specified mode of operations

void Set_Mode(OPERATION_MODE MODE){
  
  CURRENT_MODE = MODE;
  
  if(MODE == PRODUCT_PRIME_MODE){

    // Reset Seconds Counter for filler purge in Timer interrupt
    PRODUCT_PRIME_MODE_COUNTER =0;

    // Reset Filler Purge Cycle to ensure only 2 cycles happen during purge
    FILLER_PURGE_CYCLE         =0;

    // Reset Timer interrupt load value to ensure correct timing of purge cycles
    TCNT1 = T1_LOAD;

    // Begin the filler purge cycle
    
    digitalWrite(FILLER_PURGE_ON,HIGH);
  }
  if(MODE == PRODUCT_FLUSH_MODE){

    // Reset Counter Values used in cycle monitoring within timer interrupt routine
    
    PRODUCT_PRIME_MODE_COUNTER =0;
    PRODUCT_FLUSH_COUNTER=0;

    // turn on product mixer and product pump for Flush Cycle
    
    if(!digitalRead(PROD_MIXER_ON)){digitalWrite(PROD_MIXER_ON,HIGH);}
    if(!digitalRead(PROD_PUMP_ON)){digitalWrite(PROD_PUMP_ON,HIGH);}
    
    // Reset Timer interrupt load value to ensure correct timing of Fille Purge cycles
    
    TCNT1 = T1_LOAD;

    

  }
}

bool MAX31865_Init(Adafruit_MAX31865 Thermo){
  
  // init Temp Sensor
  Thermo.begin(MAX31865_4WIRE);
  
  //Check for hard faults
  uint8_t Module_Fault = Thermo.readFault();
  
  // return > 1 for fault found
  // return = 0 for no fault
  return Module_Fault;
  
}

void GPIO_Init(){

  // All Input Pins 
  
  pinMode(22,INPUT); //WATER_LEVEL_LOW
  pinMode(23,INPUT); //PROD_LEVEL_LOW
  pinMode(24,INPUT); //PROD_TOP_UP
  pinMode(29,INPUT); //BUTTON_ENTER 
  pinMode(30,INPUT); //BUTTON_DOWN
  pinMode(31,INPUT); //BUTTON_UP
  pinMode(32,INPUT); //BUTTON_RIGHT
  pinMode(33,INPUT); //BUTTON_LEFT

  // All Output Pins 
  
  pinMode(34,OUTPUT); digitalWrite(34,LOW); //12KW
  pinMode(35,OUTPUT); digitalWrite(35,LOW); //24KW
  pinMode(36,OUTPUT); digitalWrite(36,LOW); //36KW
  pinMode(37,OUTPUT); digitalWrite(37,LOW); //PROD_MIXER
  pinMode(38,OUTPUT); digitalWrite(38,LOW); //PROD_PUMP
  pinMode(39,OUTPUT); digitalWrite(39,LOW); //WATER_PUMP
  pinMode(40,OUTPUT); digitalWrite(40,LOW); //RED_LIGHT
  pinMode(41,OUTPUT); digitalWrite(41,LOW); //ORANGE_LIGHT
  pinMode(42,OUTPUT); digitalWrite(42,LOW); //GREEN_LIGHT
  pinMode(43,OUTPUT); digitalWrite(43,LOW); //SYSTEM_READY_ON
  pinMode(44,OUTPUT); digitalWrite(44,LOW); //FILLER_PURGE_ON 
  
  // Query output pin for Water Mixer mentioned in Spec Sheet
  
  
}
void Timer1_Init(){
  
    // Reset tim1 control reg A
    TCCR1A = 0;

    // set prescaler value -> 256 -> 500ms for 31250 count
    
    TCCR1B !=  (1 << CS12);
    TCCR1B &= ~(1 << CS11);
    TCCR1B &= ~(1 << CS10);

    // Reset timer and set compare value 
    
    TCNT1 = T1_LOAD;
    OCR1A = T1_COMPARE;

    // enable interrupt routine bit
    
    TIMSK1 = (1 << OCIE1A);

    // sei() enable global Interrupt routines
    
    sei();

}

// Interrupt routine for timer 1. Triggered every 500ms 

ISR(TIMER1_COMPA_vect){

  // Reset Count value of timer 
  
  TCNT1 = T1_LOAD;

  // System Light toggle statement
  
  switch(LIGHT_STATUS){
      case OK:
        // only Green Light On 
        
        if(!digitalRead(GREEN_LIGHT_ON)){digitalWrite(GREEN_LIGHT_ON,HIGH);}
        break;
      case WATER_TANK_LEVEL_LOW:
        // Toggle Green Light. Green Light Flashes 
        
        digitalWrite(GREEN_LIGHT_ON,!(digitalRead(GREEN_LIGHT_ON)));
        break;
      case PRODUCT_TOP_UP:
        // Green Light Stays on while Orange light flashes
        
        if(!digitalRead(GREEN_LIGHT_ON)){digitalWrite(GREEN_LIGHT_ON,HIGH);}
        digitalWrite(ORANGE_LIGHT_ON,!(digitalRead(ORANGE_LIGHT_ON)));
        break;
      case PROD_TEMP_UNDER:
        // Only Orange light On
        if(!digitalRead(ORANGE_LIGHT_ON)){digitalWrite(ORANGE_LIGHT_ON,HIGH);}
        break;
      case PROD_TEMP_OVER:
        // Orange Light on while red light flashes 
        
        if(!digitalRead(ORANGE_LIGHT_ON)){digitalWrite(ORANGE_LIGHT_ON,HIGH);}
        digitalWrite(RED_LIGHT_ON,!(digitalRead(RED_LIGHT_ON)));
        break;
      case PROD_LOW:
        // Only Red Light Flashing 
        
        digitalWrite(RED_LIGHT_ON,!(digitalRead(RED_LIGHT_ON)));
        break;
}

// Check current mode: Both PRIME MODE and FLUSH MODE require Filler purge cycle.

if(CURRENT_MODE == PRODUCT_PRIME_MODE || CURRENT_MODE == PRODUCT_FLUSH_MODE){

  // increment Counter to keep time in seconds. Timer interrupts at 500ms
  
  PRODUCT_PRIME_MODE_COUNTER++;

  // Check 2 second interval for filler purge signal cycle
  
  if(PRODUCT_PRIME_MODE_COUNTER == 4 && digitalRead(FILLER_PURGE_ON) &&(FILLER_PURGE_CYCLE ==0 ||  FILLER_PURGE_CYCLE ==1)){
    digitalWrite(FILLER_PURGE_ON,LOW);

    // Reset Seconds Counter
    
    PRODUCT_PRIME_MODE_COUNTER=0;

    //Increment Cycle to ensure only 2 cycles of filler purge take place
    
    FILLER_PURGE_CYCLE ++;
  }else 
    if(PRODUCT_PRIME_MODE_COUNTER == 2 && !digitalRead(FILLER_PURGE_ON) &&(FILLER_PURGE_CYCLE ==0 ||  FILLER_PURGE_CYCLE ==1)){
    digitalWrite(FILLER_PURGE_ON,HIGH);
    
  }
}

// FLUSH MODE Cycle timing 
// Once product level reaches low, timer starts and increments until PROD_PUMP_DELAY_TIME
// Delay time in seconds and counter in .5 seconds, therefor 2*ProdDelayTime

if(CURRENT_MODE == PRODUCT_FLUSH_MODE && digitalRead(PROD_LEVEL_LOW)){
    PRODUCT_FLUSH_COUNTER++;
    if(PRODUCT_FLUSH_COUNTER = 2*PROD_PUMP_DELAY_TIME){
        if(digitalRead(PROD_PUMP_ON)){digitalWrite(PROD_PUMP_ON,LOW);}
        if(digitalRead(WATER_PUMP_ON)){digitalWrite(WATER_PUMP_ON,LOW);}
        Heater_Routine(false);
    }
}
}
