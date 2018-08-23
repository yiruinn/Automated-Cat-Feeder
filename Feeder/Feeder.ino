#include <LiquidCrystal.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <Wire.h>
#include "RTClib.h"
#include <Stepper.h>

struct Time {
  int hour;
  int minute;
};

// BUTTONS
// UP == 0, DOWN == 1, LEFT == 2, RIGHT == 3
/*const int buttonUpPin = A2;
const int buttonDownPin = A0;
const int buttonRightPin = A1;
const int buttonLeftPin = A3;*/
const int buttonPins[4] = {A2, A0, A1, A3};

// debounce variables
unsigned long debounceDelay = 50;
int lastButtonStates[4] = {HIGH, HIGH, HIGH, HIGH};
int buttonStates[4];
unsigned long lastButtonTimes[4] = {0, 0, 0, 0};

// used for idle screen calculations
unsigned long lastButtonPressTime = 0;


// TEMPERATURE
const int ONE_WIRE_BUS = 10;
OneWire oneWire(ONE_WIRE_BUS); 
DallasTemperature sensors(&oneWire);

double lowTemp = 77.0;
double highTemp = 78.0;
double currentTemp;

unsigned long lastSensorCheckTime = 0;

// LCD
LiquidCrystal lcd(12,11,9,8,7,6);
const String menuItem[] = {
  "Feeder",
  "X Hour",
  "Temp(h)",
  "Temp(l)",
  "Tray 1",
  "Tray 2",
  "Tray 3",
  "Tray 4",
  "Duration",
  "Time"
};

const int LCD_LEFT_FIELD = 1;
const int LCD_RIGHT_FIELD = 10;
const int LCD_TOP_ROW = 0;
const int LCD_BOTTOM_ROW = 1;

int lcd_selection = 0;
int redraw = 1;
int isChangeValue = 0;

int isIdleScreen = 0;
unsigned long lastIdleRedrawTime = 0;

// FEEDER VARS
int isFeederActive = 0;
int feedDurationMinutes = 5;
int feedXHour = 0;
//int nextTrayPosition = 0;


//TIME
RTC_DS3231 rtc;
struct Time currentTime;
struct Time trayTime[] = {
  {3,50},
  {4,4},
  {11,3},
  {18,23}
  };

// STEPPER MOTOR
const int stepsPerRevolution = 64;
  // 2048 total steps per rotation
int steps[5] = {468, 370, 370, 370, 470};
int currentPosition = 0;
int motorStepsLeft = 0;
int isCalibrated = 0;
  // Order is wrong on purpose due to motor wire mismatch
Stepper myStepper(stepsPerRevolution, 2, 4, 3, 5);

// RELAY
int isCoolerOn = 0;

// IR SENSORS
const int calibrationSensorPin = A7;
const int obstructionSensorPin = A6;


void changeSelectionValue(int value) {
  // value will either be +-1, +-10
  switch(lcd_selection) {
    case 0:
      {
        // activate/deactivate feeder
        int newValue = isFeederActive + value;
        if(newValue >= 0 && newValue <= 1) isFeederActive = newValue;
        if(isFeederActive == 1 && feedXHour != 0) {
          calculateXHourTimes();
        }
      }
      break;
    case 1:
      // change x hour
      changeIntOf(&feedXHour, value);
      break;
    case 2:
      // change temp(high)
      changeTemperatureOf(&highTemp, value * 0.1);
      break;
    case 3:
      // change temp(low)
      changeTemperatureOf(&lowTemp, value * 0.1);
      break;
    case 4:
      // change tray time 1
      changeTimeOf(&trayTime[0], value);
      break;
    case 5:
      // change tray time 2
      changeTimeOf(&trayTime[1], value);
      break;
    case 6:
      // change tray time 3
      changeTimeOf(&trayTime[2], value);
      break;
    case 7:
      // change tray time 4
      changeTimeOf(&trayTime[3], value);
      break;
    case 8:
      // change feeding duration
      changeIntOf(&feedDurationMinutes, value);
      break;
    case 9:
      // change current time
      changeTimeOf(&currentTime, value);
      break;
  }
}

// use to change trayTime
void changeTimeOf(struct Time *t, int value) {
  int newValue = t->minute + value;
  if(newValue > 59) t->hour += 1;
  else if(newValue < 0) t->hour -= 1;
  t->minute = (newValue + 60) % 60;
  t->hour = (t->hour + 24) % 24;
}

// use to change highTemp and lowTemp
void changeTemperatureOf(double *d, double value) {
  double newValue = *d + value;
  if(newValue >= 0.0 && newValue < 100.0) *d = newValue;
}

// use to change feedXHour and feedDurationMinutes
void changeIntOf(int *i, int value) {
  int newValue = *i + value;
  if(newValue >= 0) *i = newValue;
}

void calculateXHourTimes() {
  for(int i = 0; i < 4; i++) {
    trayTime[i].hour = (currentTime.hour + feedXHour*(i+1)) % 24;
    trayTime[i].minute = currentTime.minute;
  }
}

void moveCursorTo(int selection) {
  int position = selection % 2;
  lcd.setCursor(0, position);
  lcd.print(">");
  lcd.setCursor(0, abs(position - 1));
  lcd.print(" ");
  if(isChangeValue == 1) {
    lcd.setCursor(0, position);
    lcd.blink();
  } else {
    lcd.noBlink();
  }
}

void lcdRedraw() {
  lcd.clear();
  redraw = 0;
  char timebuffer[5];

  if(isIdleScreen == 1) {
    lcd.setCursor(LCD_LEFT_FIELD, LCD_TOP_ROW);
    lcd.print("Current:");
    lcd.setCursor(LCD_RIGHT_FIELD+4, LCD_TOP_ROW);
    lcd.print(currentPosition);
    lcd.setCursor(LCD_LEFT_FIELD,LCD_BOTTOM_ROW);
    sprintf(timebuffer, "%02d:%02d",currentTime.hour,currentTime.minute);
    lcd.print(timebuffer);
    lcd.setCursor(LCD_RIGHT_FIELD-1,LCD_BOTTOM_ROW);
    lcd.print(currentTemp);
    lcd.print("F");

    lastIdleRedrawTime = millis();
    return;
  }
  
  // calculate which section to display based on current selection
  int section = lcd_selection / 2;
  
  // display menu item associated with section 
  lcd.setCursor(LCD_LEFT_FIELD, LCD_TOP_ROW);
  lcd.print(menuItem[section * 2]);
  lcd.setCursor(LCD_LEFT_FIELD, LCD_BOTTOM_ROW);
  lcd.print(menuItem[section * 2 + 1]);
  
  // display values associated with menu item
  switch(section) {
    case 0:
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      if(isFeederActive == 0) lcd.print("Inactive");
      else lcd.print("Active");
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_BOTTOM_ROW);
      lcd.print(feedXHour);
      break;
    case 1:
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      lcd.print(highTemp);
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_BOTTOM_ROW);
      lcd.print(lowTemp);
      break;
    case 2:
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      sprintf(timebuffer, "%02d:%02d",trayTime[0].hour,trayTime[0].minute);
      lcd.print(timebuffer);
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_BOTTOM_ROW);
      sprintf(timebuffer, "%02d:%02d",trayTime[1].hour,trayTime[1].minute);
      lcd.print(timebuffer);
      break;
    case 3:
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      sprintf(timebuffer, "%02d:%02d",trayTime[2].hour,trayTime[2].minute);
      lcd.print(timebuffer);
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_BOTTOM_ROW);
      sprintf(timebuffer, "%02d:%02d",trayTime[3].hour,trayTime[3].minute);
      lcd.print(timebuffer);
      break;
    case 4:
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      lcd.print(feedDurationMinutes);
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_BOTTOM_ROW);
      sprintf(timebuffer, "%02d:%02d",currentTime.hour,currentTime.minute);
      lcd.print(timebuffer);
      break;
    default:
      // should never hit this case
      lcd.setCursor(LCD_RIGHT_FIELD, LCD_TOP_ROW);
      lcd.print("ERROR");
  }

  // display cursor
  moveCursorTo(lcd_selection);
}

void buttonPressed(int button, int multiplier) {
  // UP == 0
  // DOWN == 1
  // LEFT == 2
  // RIGHT == 3

  // set last button press time for idle screen calculations 
  lastButtonPressTime = millis();
  // always redraw on button press
  redraw = 1;
  // only exit idle screen on first button press on idle screen
  if(isIdleScreen == 1) {
    isIdleScreen = 0;
    return;
  }

  // execute code depending on what button pressed
  switch(button) {
    case 0:
      if(isChangeValue == 1) changeSelectionValue(multiplier);
      // supposed to be -1 + 10: -1 for decreasing menu selection, +10 for making the % operation always positive
      else lcd_selection = (lcd_selection + 9) % 10;
      break;
    case 1:
      if(isChangeValue == 1) changeSelectionValue(-multiplier);
      else lcd_selection = (lcd_selection + 1) % 10;
      break;
    case 2:
      isChangeValue = 1;
      break;
    case 3:
      isChangeValue = 0;
      if(lcd_selection == 9) setCurrentTime();
      break;
  }
}

double getCurrentTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempFByIndex(0);
}

void setCurrentTime() {
  // grab whatever date is currently stored, date doesn't matter for this program
  DateTime now = rtc.now();
  rtc.adjust(DateTime(now.year(), now.month(), now.day(), currentTime.hour, currentTime.minute));
}

void setMoveTo(int destPosition) {
  int currentStep = getStepOf(currentPosition);
  int destStep = getStepOf(destPosition);
  // calculates steps to go ccw or cw
  int diffStep = (destStep - currentStep + 2048) % 2048;
  // chooses direction based on which way is shorter
  if(diffStep < 1024) {
    // clockwise
    //Serial.println("cw");
    //Serial.println(diffStep);
    motorStepsLeft = diffStep;
  } else {
    // counter-clockwise
    //Serial.println("ccw");
    //Serial.println(diffStep - 2048);
    motorStepsLeft = diffStep - 2048;
  }
  currentPosition = destPosition;
}

int getStepOf(int position) {
  int ret = 0;
  for(int i = 0; i < position; i++) {
    ret += steps[i];
  }
  return ret;
}

void calibrateMotor() {
  // turn motor to 0 position
  while(analogRead(calibrationSensorPin) < 512) {
    //Serial.println(analogRead(calibrationSensorPin));
    myStepper.step(1);
  }
  myStepper.step(-110);
  currentPosition = 1;
  isCalibrated = 1;
}

int getFeedingPosition() {
  for(int i = 0; i < 4; i++) {
    int timeDiff = (currentTime.hour - trayTime[i].hour) * 60 + currentTime.minute - trayTime[i].minute;
    if(timeDiff >= 0 && timeDiff <= feedDurationMinutes) {
      // return respective tray position corresponding to feeding time
      return i + 1;
    }
  }
  // return default tray position (number 0) if not within feeding time
  return 0;
}

void setup() {
  Serial.begin(9600);
  delay(1000);

  pinMode(13, INPUT_PULLUP);
  pinMode(13, OUTPUT);

  pinMode(A6, INPUT);
  pinMode(A7, INPUT);
  
  digitalWrite(13, HIGH);

  
  // Buttons with internal pull-up resistors
  pinMode(buttonPins[0], INPUT_PULLUP);
  pinMode(buttonPins[1], INPUT_PULLUP);
  pinMode(buttonPins[2], INPUT_PULLUP);
  pinMode(buttonPins[3], INPUT_PULLUP);
  
  // Using a 16x2 lcd
  lcd.begin(16,2);

  // Write something to LCD
  lcd.setCursor(LCD_LEFT_FIELD, LCD_TOP_ROW);
  lcd.print("CAT FEEDER");
  lcd.setCursor(LCD_LEFT_FIELD,LCD_BOTTOM_ROW);
  lcd.print("INITIALIZING...");

  // Temperature sensor init
  sensors.begin();

  // Motor init
  myStepper.setSpeed(60);

  // Timer init
  rtc.begin();
  // The following line breaks D13 for some reason
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));


  currentTemp = getCurrentTemperature();
  DateTime now = rtc.now();
  currentTime.hour = now.hour();
  currentTime.minute = now.minute();
  
  Serial.println("EXECUTING SETUP");
}

void loop() {
  // MOTOR CALLIBRATION: BLOCKING; can this be put in setup?
  if(!isCalibrated) calibrateMotor();

  //digitalWrite(13, LOW);

  // OBSTRUCTION SENSOR: stop doing everything and wait till clear
  if(analogRead(obstructionSensorPin) < 512) {
    delay(1000);
    return;
  }
  
  // MOTOR MOVEMENT
  if(motorStepsLeft > 0) {
    myStepper.step(1);
    motorStepsLeft -= 1;
  } else if(motorStepsLeft < 0) {
    myStepper.step(-1);
    motorStepsLeft += 1;
  }

  // SENSORS: checking every loop is too heavy and lags the menu. Only check sensor every 3 seconds
  if(millis() - lastSensorCheckTime > 3000) {
    //Serial.println("SENSOR CHECK");
    lastSensorCheckTime = millis();
    // set currentTemp variable
    // getting current temperature is a expensive operation, causes lag. Only get when nothing else is happening
    if(isIdleScreen == 1 && motorStepsLeft == 0) currentTemp = getCurrentTemperature();
    if(currentTemp > highTemp && isCoolerOn == 0) {
      // turn on cooler
      digitalWrite(13, LOW);
      isCoolerOn = 1;
    } else if(currentTemp <= lowTemp && isCoolerOn == 1) {
      // turn off cooler
      digitalWrite(13, HIGH);
      isCoolerOn = 0;
    }
    // set currentTime variable, don't update when user is trying to change it
    if(lcd_selection != 9) {
      DateTime now = rtc.now();
      currentTime.hour = now.hour();
      currentTime.minute = now.minute();
    }
     // check if is feeding time
    if(motorStepsLeft == 0) {
      int targetPosition = getFeedingPosition();
      if(targetPosition != currentPosition) {
        // if feeder is active, allow it to move to any position
        if(isFeederActive == 1) setMoveTo(targetPosition);
        // otherwise, only allow it to move to 0
        else if(targetPosition == 0) setMoveTo(0);
        
        // deactivate feeder if on fourth tray
        if(targetPosition == 4) isFeederActive = 0;
      }
    }
  }

  // IDLE SCREEN DETECTION: 20 seconds for idle screen
  if(millis() - lastButtonPressTime >= 20000 && isIdleScreen == 0) {
    isIdleScreen = 1;
    redraw = 1;
    // reset current menu display
    lcd_selection = 0;
    isChangeValue = 0;
    lcd.noBlink();
  }

  // BUTTONS: check and debounce buttons
  int reading;
  // iterate through the buttons
  // UP == 0, DOWN == 1, LEFT == 2, RIGHT == 3
  for(int i = 0; i < 4; i++) {
    reading = digitalRead(buttonPins[i]);
    // if button state has changed
    if (reading != lastButtonStates[i]) {
      // reset the debouncing timer
      lastButtonTimes[i] = millis();
    }
    lastButtonStates[i] = reading;
    if (millis() - lastButtonTimes[i] > debounceDelay) {
      if (reading != buttonStates[i]) { // single button press
        buttonStates[i] = reading;
        if(buttonStates[i] == LOW) buttonPressed(i, 1);
      } else if(millis() - lastButtonTimes[i] > 500 && buttonStates[i] == LOW && (i == 0 || i == 1)) { // continued holding of up or down button
        lastButtonTimes[i] = millis();
        buttonPressed(i, 10);
      }
    }
  }

  // need to redraw on idle screen for time and temperature, update every 5 seconds to prevent flashing
  if(redraw == 1 || (isIdleScreen == 1 && millis() - lastIdleRedrawTime > 5000)) {
    lcdRedraw();
  }
}
