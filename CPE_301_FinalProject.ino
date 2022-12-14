// Foster Schmidt
// Dec 13, 2022
// CPE_301_Fall_2022
// Final Project

#include "Arduino.h"
#include "DHT.h"
#include "Stepper.h"
#include "LiquidCrystal.h"
#include "uRTCLib.h"

// Stepper Motor
const int stepsPerRevolution = 2048;
Stepper myStepper = Stepper(stepsPerRevolution, 31, 35, 33, 37);
volatile unsigned int potVal1, potVal2 = 0;

// Temp & Humi sensor
#define DHTPIN 7
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
const unsigned int tempThres = 74;
volatile float currentTemp = 0;
volatile float currentHumi = 0;

// LCD settings and intilization
const int rs = 12, en = 11, d4 = 6, d5 = 5, d6 = 4, d7 = 3;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
volatile unsigned long lastUpdate = -60000;

// RTC module
uRTCLib rtc(0x68);
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Water level threshold
const unsigned int waterThres = 100;
volatile unsigned int currentWater = 0;

// ENUM States for event control
enum state{
  disabled,
  idle,
  running_,
  error
};

// Instatializing states for use
volatile state currentState = disabled;
volatile state lastState = disabled;
 
// Functions
void setADCRegister(byte port);
unsigned int readADC();
void fanMode(state s);
void ventAdjust();
void monitorWater();
void monitorTemp();
void printError();
void turnLEDOn(state s);
void turnLEDOff(state s);
void turnOnInterrupt(state s);
void turnOffInterrupt(state s);
void updateLCD();
void clearLCD();
ISR(INT2_vect);
ISR(INT3_vect);
ISR(INT4_vect);

void setup() {
  Serial.begin(9600);
  URTCLIB_WIRE.begin();
  //rtc.set(0, 30, 12, 5, 8, 12, 22);
  dht.begin();
  lcd.begin(16, 2);
  myStepper.setSpeed(10);
  DDRA |= 0b10101010; // State LED's D29, D27, D25, D23
  DDRL = (1 << PL6); // Motor switch D43
  EICRA |= 0b11110000; //External interrupts on INT3(stop), INT2(start) on rising edge
  EICRB |= 0b00000011; //External interrupts on INT4(reset) on rising edge
}

void loop() {
  rtc.refresh();
  if(currentState != lastState){
    lastUpdate = -60000;
    clearLCD();
    turnOffInterrupt(lastState);
    turnLEDOff(lastState);
    lastState = currentState;
    Serial.print("The state was changed at ");
    Serial.print(rtc.hour());
    Serial.print(':');
    Serial.print(rtc.minute());
    Serial.print(':');
    Serial.print(rtc.second());
    Serial.print("\n");
  }
  fanMode(currentState);
  ventAdjust();
  switch (currentState){
    case disabled:
      clearLCD();
      break;
    case idle:
      monitorTemp();
      monitorWater();
      if(currentWater < waterThres){
        currentState = error;
        break;
      }
      if(currentTemp > tempThres){
        currentState = running_;
      }
      updateLCD();
      break;
    case running_:
      monitorTemp();
      monitorWater();
      if(currentWater < waterThres){
        currentState = error;
        break;
      }
      if(currentTemp <= tempThres){
        currentState = idle;
      }
      updateLCD();
      break;
    case error:
      monitorTemp();
      printError();
      break;
  }
  EIFR |= 0b11111111;
  turnOnInterrupt(currentState);
  turnLEDOn(currentState);
}

void setADCRegister(byte port){
  ADMUX &= 0b00000000; // clearing register
  if(port == 1){
    ADMUX |= 0b01000001; // setting Vref to AVCC, and setting input to A1
  }
  else if(port == 2){
    ADMUX |= 0b01000010; // setting Vref to AVCC, and setting input to A2
  }
  ADCSRB &= 0b00000000; // clearing register 
  ADCSRB &= 0b00000000; // MUX5 is bit 3, and for now I will use single conversion mode
  ADCSRA &= 0b00000000; // clearing register
  ADCSRA |= 0b10000011; // enabling ADC
}

unsigned int readADC(){
  unsigned int convValue;
  ADCSRA |= 0b01000000; // starting conversion
  while((ADCSRA & 0b01000000) != 0); // waiting till we have a conversion
  convValue = (ADCL | (ADCH << 8));
  return convValue;
}

void fanMode(state s){
  if(s == running_){
    PORTL = (1 << PL6);
  }
  else{
    PORTL = (0 << PL6);
  }
}
void ventAdjust(){
  setADCRegister(2);
  potVal1 = map(readADC(), 0, 1023, 0, 31);
  if(potVal1 > potVal2){
    myStepper.step(40);
    Serial.print("Vent going up \n");
  }
  else if(potVal1 < potVal2){
    myStepper.step(-40);
    Serial.print("Vent going down \n");
  }
  potVal2 = potVal1;
}

void monitorWater(){
  setADCRegister(1);
  currentWater = readADC();
}

void monitorTemp(){
  currentTemp = dht.readTemperature(true);
  currentHumi = dht.readHumidity();
}

void printError(){
  lcd.setCursor(5,0);
  lcd.print("ERROR!");
  lcd.setCursor(1,1);
  lcd.print("WATER TOO LOW!");
}

void turnLEDOn(state s){
  if(s == disabled){
    PORTA = (1 << PA1);
  }
  else if(s == idle){
    PORTA = (1 << PA3);
  }
  else if(s == running_){
    PORTA = (1 << PA5);
  }
  else if(s == error){
    PORTA = (1 << PA7);
  }
}

void turnLEDOff(state s){
   if(s == disabled){
     PORTA = (0 << PA1);
  }
  else if(s == idle){
     PORTA = (0 << PA3);
  }
  else if(s == running_){
     PORTA = (1 << PA5);
  }
  else if(s == error){
     PORTA = (1 << PA7);
  }
}

void turnOnInterrupt(state s){
  if(s == disabled){
    EIMSK |= 0b0000100;
  }
  else if(s == idle || s == running_){
    EIMSK |= 0b00001000;
  }
  else if(s == error){
    EIMSK |= 0b00011000;
  }
}

void turnOffInterrupt(state s){
  if(s == disabled){
    EIMSK &= 0b11111011;
  }
  else if(s == idle || s == running_){
    EIMSK &= 0b11110111;
  }
  else if(s == error){
    EIMSK &= 0b11100111;
  }
}

void updateLCD(){
  unsigned long currentTime = millis();
  if(currentTime - lastUpdate > 60000){
    lcd.setCursor(0,0);
    lcd.print("Temp is: ");
    lcd.print(currentTemp);
    lcd.setCursor(0,1);
    lcd.print("Humi is: ");
    lcd.print(currentHumi);
    lastUpdate = currentTime;
  }
}

void clearLCD(){
  lcd.clear();
}

ISR(INT2_vect){
  turnOffInterrupt(currentState);
  currentState = idle;
}

ISR(INT3_vect){
  turnOffInterrupt(currentState);
  currentState = disabled;
}

ISR(INT4_vect){
  turnOffInterrupt(currentState);
  currentState = idle;
}
