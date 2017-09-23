/****************
 * CODE OPTIONS *
 ****************/
// Enable debug (aka serial print)
#define DEBBYUG 1
#define SERIAL_BAUD 9600
// pin of the backlight of LCD
#define SCREENLIGHT 9
// PWM out for the light
#define EXTLIGHT 10
// input for IR reade
#define IR 2
// pins for LCD
#define LCD_RS 3
#define LCD_E 4
#define LCD_D4 5
#define LCD_D5 6
#define LCD_D6 7
#define LCD_D7 8

/*******************************************
 * ARRAYS FOR REMOTE CONTROL CONFIGURATION *
 *******************************************/
unsigned long signalsUP[] = { 1, 0xFF906F };
unsigned long signalsDOWN[] = { 1, 0xFFE01F };
unsigned long signalsPOWER[] = { 1, 0xFFA25D };
unsigned long signalsBACKLIGHT[] = { 1, 0xFF629D };
unsigned long signalsFUNCTION[] = { 1, 0xFFE21D };
unsigned long signalsOK[] = { 1, 0xFF02FD };
unsigned long signalsZERO[] = { 1, 0xFF6897 };
unsigned long signalsONE[] = { 1, 0xFF30CF };
unsigned long signalsTWO[] = { 1, 0xFF18E7 };
unsigned long signalsTHREE[] = { 1, 0xFF7A85 };
unsigned long signalsFOUR[] = { 1, 0xFF10EF };
unsigned long signalsFIVE[] = { 1, 0xFF38C7 };
unsigned long signalsSIX[] = { 1, 0xFF5AA5 };
unsigned long signalsSEVEN[] = { 1, 0xFF42BD };
unsigned long signalsEIGHT[] = { 1, 0xFF4AB5 };
unsigned long signalsNINE[] = { 1, 0xFF52AD };

/****************
 * DEBUG MACROS *
 ****************/
#if DEBBYUG == 1
#define NDEBUGPRINT(x) Serial.print(millis()); \
    Serial.print(": "); \
    Serial.print(__PRETTY_FUNCTION__); \
    Serial.print(' '); \
    Serial.print(__FILE__); \
    Serial.print(':'); \
    Serial.print(__LINE__); \
    Serial.print(' '); \
    Serial.println(x)
#define DEBUGPRINT(x) Serial.println(x)
#else
#define NDEBUGPRINT(x)
#define DEBUGPRINT(x)
#endif

/*****************
 *   LIBRARIES   *
 *****************/
#include "Arduino.h"
#include <IRremote.h>
#include <DS3232RTC.h>
#include <TimeLib.h>
#include <Wire.h>
#include <LiquidCrystal.h>

/********************
 * GLOBAL VARIABLES *
 ********************/
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
IRrecv ir(IR);
decode_results irResult; // var to store result of the IR reader
char prBuffer[20]; // a buffer to store a string to print in the lcd
bool mainProspective = 1; // a bool var to store if the main prospective display hour/data (1) or hour/alarm (0)
bool alarmStatus = 0;  // if alarm is enabled
int alarmHour = 7; // alarm hour
int alarmMinute = 0; // alarm minute
int alarmAdvance = 30; // minutes of advance from whitch start to turn on (gradually) light
unsigned long lastAlarmRung = 0; // store timestamp where alarm rung for last time (is needed to avoid that alarm starts again when i set it on after it rung)

/*******************
 *   FUNCTIONS     *
 *******************/

/**
 * Return true if a value is in an array else return false
 * @param unsigned long signalsArray is an array with all good values (firis value is the len of the array)
 * @param unsigned long signal is the signal to look for
 * @return bool
 */
bool isSignalInArray(unsigned long signalsArray[], unsigned long signal) {
    int arrayLen = signalsArray[0];
    for (int i = 1; i <= arrayLen; i++) {
        if (signal == signalsArray[i])
            return true;
    }
    return false;
}

/**
 * Empty a 2x16 screen and leave cursor at the end
 *
 */
void lcdReset() {
    lcd.setCursor(0, 0);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
}

/**
 * Empty a 2x16 screen and leave cursor at the beginning (ready to print)
 *
 */
void lcdFullReset() {
    lcdReset();
    lcd.setCursor(0, 0);
}

/**
 * Set the cursor at the beginning of a line
 *
 */
void cursorReset(int line) {
    lcd.setCursor(0, line);
}

/**
 * Set date/hour and save result to the RTC
 * @param int ho the hour to set
 * @param int mi the minute
 * @param int se the second
 * @param int da the date
 * @param int mo the month
 * @param int ye the year
 *
 */
void setInsertTime(int ho, int mi, int se, int da, int mo, int ye) {
    // ho mi se da mo ye
    setTime(ho, mi, se, da, mo, ye);
    RTC.set(now());
}

/**
 * Change the screen status (turn it on if it is off and vice versa)
 *
 */
void changeScreenStatus() {
    if (digitalRead(SCREENLIGHT) == LOW) {
        lcd.display();
        digitalWrite(SCREENLIGHT, HIGH);
    }
    else {
        lcd.noDisplay();
        digitalWrite(SCREENLIGHT, LOW);
    }
}

/**
 * Turn on the screen (turn it on if it was off and leave on if it was on)
 * but at the end leave the screen in the state it was at the origin
 * so you MUST call this function ALWAYS two times to restore screen status
 *
 * Sintetically when you want the screen is on you call this function and when
 * the previus status could be restored you call again
 */
int changeScreenStatusWithMemory() {
    static int call = 1;
    static int previusLightStatus;
    if (call) {
        previusLightStatus = digitalRead(SCREENLIGHT);

        if (previusLightStatus == LOW) {
            lcd.display();
            digitalWrite(SCREENLIGHT, HIGH);
        }
        call = 0;
    }
    else {
        if (previusLightStatus == LOW) {
            lcd.noDisplay();
            digitalWrite(SCREENLIGHT, LOW);
        }
        call = 1;
    }
    return call;
}

/**
 * Set the output light
 * @param int percent the percent of the light brightness (0 off, 100 full power)
 */
void setLight(int percent) {
    if (percent <= 0) {
        NDEBUGPRINT("Set light LOW");
        digitalWrite(EXTLIGHT, LOW);
    }
    else if (percent >= 100) {
        NDEBUGPRINT("Set light HIGH");
        digitalWrite(EXTLIGHT, HIGH);
    }
    else {
        analogWrite(EXTLIGHT, map(percent, 0, 100, 0, 255));
        NDEBUGPRINT("Set light LOW as");
        DEBUGPRINT(map(percent, 0, 100, 0, 255));
    }
}

/* This function returns the DST offset for the current UTC time.
 This is valid for the EU, for other places see
 http://www.webexhibits.org/daylightsaving/i.html

 Results have been checked for 2012-2030 (but should work since
 1996 to 2099) against the following references:
 - http://www.uniquevisitor.it/magazine/ora-legale-italia.php
 - http://www.calendario-365.it/ora-legale-orario-invernale.html

 @param int d the day
 @param int m the month
 @param int y the year
 @param int h the hour
 @return bool (true if legal false if solar)
 */
bool dstOffset(int d, int m, unsigned int y, int h) {

    // Day in March that DST starts on, at 1 am
    int dstOn = (31 - (5 * y / 4 + 4) % 7);

    // Day in October that DST ends  on, at 2 am
    int dstOff = (31 - (5 * y / 4 + 1) % 7);

    if ((m > 3 && m < 10) || (m == 3 && (d > dstOn || (d == dstOn && h >= 1))) || (m == 10 && (d < dstOff || (d == dstOff && h <= 1))))
        return 1;
    else
        return 0;
}

/**
 * Simple interface for dstOffset function
 * @return bool
 */
bool nowDstOffset() {
    return dstOffset(day(), month(), year(), hour());
}

/*******************
 *     SETUP       *
 *******************/
void setup() {
#if DEBBYUG == 1
    Serial.begin(SERIAL_BAUD);
#endif

    // Set pins
    pinMode(SCREENLIGHT, OUTPUT);
    digitalWrite(SCREENLIGHT, LOW);

    pinMode(EXTLIGHT, OUTPUT);
    digitalWrite(EXTLIGHT, LOW);

    // Load wire
    Wire.begin();

    // Load Screen
    lcd.begin(16, 2);

    // Turn on backlight
    digitalWrite(SCREENLIGHT, HIGH);

    // Load clock
    setSyncProvider(RTC.get);

    // Load IR
    ir.enableIRIn();

}

/*******************
 *      LOOP       *
 *******************/
void loop() {
    // Show the standard view
    updateStandardViewTrigger();

    // Alarm check
    checkAlarmTrigger();

    if (ir.decode(&irResult)) {
        // Standard view change (if show time/date or time/alarm)
        if (isSignalInArray(signalsUP, irResult.value) || isSignalInArray(signalsDOWN, irResult.value)) {
            mainProspective = mainProspective ? 0 : 1;
        }

        // Alarm On Off
        else if (isSignalInArray(signalsOK, irResult.value)) {
            alarmStatus = alarmStatus ? 0 : 1;
            // if turn on alarm
            if (alarmStatus) {
                for (int i = 0; i < 10; i++) {
                    changeScreenStatus();
                    delay(100);
                    updateStandardViewTrigger();
                }
            }
            // turn off alarm
            else {
                for (int i = 0; i < 4; i++) {
                    changeScreenStatus();
                    delay(500);
                    updateStandardViewTrigger();
                }
            }
        }

        // Light triggers
        else if (isSignalInArray(signalsPOWER, irResult.value)) {
            // If screen light is off i turn on
            changeScreenStatusWithMemory();

            lightOnTrigger();

            // If needed turn off light
            changeScreenStatusWithMemory();
        }

        // BACKLIGHT
        else if (isSignalInArray(signalsBACKLIGHT, irResult.value)) {
            changeScreenStatus();
        }

        // MENU
        else if (isSignalInArray(signalsFUNCTION, irResult.value)) {
            // If screen light is off i turn on
            changeScreenStatusWithMemory();

            menuTrigger();

            // If needed turn off light
            changeScreenStatusWithMemory();
        }

#if DEBBYUG == 1
        //DEBUGPRINT(irResult.value);
#endif

        ir.resume();
    }
}

/*******************
 *    TRIGGERS     *
 *******************/

/**
 * This trigger update the standard view (there are 2 possible views time/date or time/alarm)
 *
 */
void updateStandardViewTrigger() {
    // Default view
    if (mainProspective) {
        cursorReset(0);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour() + nowDstOffset(), minute(), second());
        lcd.print(prBuffer);
        cursorReset(1);
        sprintf(prBuffer, "   %02d/%02d/%04d   ", day(), month(), year());
        lcd.print(prBuffer);
    }
    // Alarm view
    else {
        cursorReset(0);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour() + nowDstOffset(), minute(), second());
        lcd.print(prBuffer);
        cursorReset(1);

        sprintf(prBuffer, " ALR %s %02d:%02d  ", alarmStatus ? "ON " : "OFF", alarmHour, alarmMinute);
        lcd.print(prBuffer);
    }
}

/**
 * This trigger check if alarm must ring and if it must loadalarmRingTrigger()
 *
 */
void checkAlarmTrigger() {
    // check only if alarm status is on (true)
    if (alarmStatus) {
        int lAlarmMinute = alarmMinute;
        int lAlarmHour = alarmHour;

        // if i subtract minutes and the hour don't change I do it
        if (lAlarmMinute - alarmAdvance >= 0) {
            lAlarmMinute -= alarmAdvance;
        }
        // else i subtract an hour and add 60 minutes and then i subtract
        else {
            lAlarmHour--;
            if (lAlarmHour < 0) {
                lAlarmHour = 23;
            }
            lAlarmMinute += (60 - alarmAdvance);
        }
        // If it's time to ring and i have not rung alarm yet I load alarm trigger
        if (hour() + nowDstOffset() == lAlarmHour && minute() == lAlarmMinute && (now() - lastAlarmRung) > 60 * alarmAdvance) {
            DEBUGPRINT(now());
            DEBUGPRINT(lastAlarmRung);
            lastAlarmRung = now();
            //alarmStatus = 0; // disable alarm, but why?
            alarmRingTrigger();
        }
    }
}

/**
 * This trigger wake up with light!!
 *
 */
void alarmRingTrigger() {
    int light = 0;
    lcdFullReset();
    lcd.print("     ALARM!     ");
    setLight(light);
    int startMinute = minute();
    int countMinutes = 0;
    while (true) {
        // If next minute
        if (startMinute != minute()) {
            startMinute = minute();
            countMinutes++;
            NDEBUGPRINT("countMinutes:");
            DEBUGPRINT(countMinutes);
        }
        // light change if needed
        //int nLight = (float) countMinutes/alarmAdvance*100;
        int nLight = map(countMinutes, 0, alarmAdvance, 0, 100);
        if (light != nLight && nLight <= alarmAdvance) {
            light = nLight;
            setLight(light);
            NDEBUGPRINT("Changing light to:");
            DEBUGPRINT(light);
        }
        ir.resume();
        delay(200);

        // hour print
        cursorReset(1);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour() + nowDstOffset(), minute(), second());
        lcd.print(prBuffer);

        // exit
        if (ir.decode(&irResult)) {
            if (isSignalInArray(signalsPOWER, irResult.value)) {
                setLight(0);
                ir.resume();
                delay(200);
                return;
            }
        }
    }
}

/**
 * This trigger turn on the light and allow to change brightness
 */
void lightOnTrigger() {
    static int lightPercent = 100;
    lcdFullReset();
    cursorReset(1);
    lcd.print("   Light ");
    if (lightPercent < 100)
        lcd.print(" ");
    lcd.print(lightPercent);
    lcd.print("%   ");
    setLight(lightPercent);

    int cycle = 1;
    while (cycle) {
        cursorReset(0);
        sprintf(prBuffer, "    %02d:%02d:%02d    ", hour() + nowDstOffset(), minute(), second());
        lcd.print(prBuffer);

        ir.resume();
        delay(200);
        if (ir.decode(&irResult)) {
            // if poweroff button
            if (isSignalInArray(signalsPOWER, irResult.value)) {
                cycle = 0;
            }
            else if (isSignalInArray(signalsUP, irResult.value) && lightPercent <= 90) {
                lightPercent += 10;
                cursorReset(1);
                lcd.print("   Light ");
                if (lightPercent < 100)
                    lcd.print(" ");
                lcd.print(lightPercent);
                lcd.print("%   ");
                setLight(lightPercent);
            }
            else if (isSignalInArray(signalsDOWN, irResult.value) && lightPercent >= 10) {
                lightPercent -= 10;
                cursorReset(1);
                lcd.print("   Light ");
                if (lightPercent < 100)
                    lcd.print(" ");
                if (lightPercent == 0)
                    lcd.print(" ");
                lcd.print(lightPercent);
                lcd.print("%   ");
                setLight(lightPercent);
            }
        }
    }

    // Turn off light
    setLight(0);
}

void menuTrigger() {
    //MAGIC
    int menuLen = 2; // items excluding exit :D

    int menuStatus = 0;

    while (menuStatus >= 0) {
        cursorReset(0);
        lcd.print("MENU:           ");

        ir.resume();
        delay(200);
        cursorReset(1);

        bool decode = ir.decode(&irResult);

        // menu navigation
        // down
        if (decode && menuStatus > 0 && isSignalInArray(signalsDOWN, irResult.value)) {
            DEBUGPRINT("--");
            menuStatus--;
        }
        // up
        else if (decode && menuStatus < menuLen && isSignalInArray(signalsUP, irResult.value)) {
            menuStatus++;
            DEBUGPRINT("++");
        }

        // if up overflow reset
        else if (decode && menuStatus == menuLen && isSignalInArray(signalsUP, irResult.value)) {
            menuStatus = 0;
        }

        // if down overflow reset
        else if (decode && menuStatus == 0 && isSignalInArray(signalsDOWN, irResult.value)) {
            menuStatus = menuLen;
        }

        // MENU
        // exit 0
        if (menuStatus == 0) {
            lcd.print("exit            ");
            // if pressed func (enter)
            if (decode && isSignalInArray(signalsFUNCTION, irResult.value)) {
                menuStatus--;
            }
        }

        // Set alarm
        else if (menuStatus == 1) {
            lcd.print("Set alarm       ");

            // ip pressed func (enter)
            if (decode && isSignalInArray(signalsFUNCTION, irResult.value)) {
                setAlarmTrigger();
            }
        }

        // Set date and hour
        else if (menuStatus == 2) {
            lcd.print("Set date & hour ");

            // ip pressed func (enter)
            if (decode && isSignalInArray(signalsFUNCTION, irResult.value)) {
                setDateAndHourTrigger();
            }
        }
    }
}

void setAlarmTrigger() {
    cursorReset(0);
    lcd.print("   Alarm  Set   ");
    cursorReset(1);
    lcd.print("     --:--      ");
    int inputData[4];
    lcd.setCursor(5, 1);
    for (int i = 0; i < 4; i++) {
        inputData[i] = getInputNumber();

        if (i == 2) {
            lcd.print(":");
        }
        lcd.print(inputData[i]);
    }

    // Here compute
    int hour = 10 * inputData[0] + inputData[1];
    int minute = 10 * inputData[2] + inputData[3];

    //check if accettable
    if (hour > 24 or minute > 60) {
        cursorReset(0);
        lcd.print("ERROR!! Input is");
        cursorReset(1);
        lcd.print("not accettable! ");
        delay(2000);
        return;
    }

    alarmHour = hour;
    alarmMinute = minute;
    lastAlarmRung = 0;

    // Print
    cursorReset(0);
    lcd.print("     Done...    ");
    delay(500);
    cursorReset(1);
    lcd.print("      :-)       ");
    delay(1000);

}

void setDateAndHourTrigger() {
    // Beauty
    cursorReset(0);
    lcd.print("Time: --:--:--  ");
    cursorReset(1);
    lcd.print("Date: --/--/----");

    int inputData[14];

    cursorReset(0);

    // Select time
    lcd.print("Time: ");
    for (int i = 0; i < 6; i++) {
        inputData[i] = getInputNumber();

        if (i == 2 || i == 4) {
            lcd.print(":");
        }

        lcd.print(inputData[i]);
    }

    // Select date
    cursorReset(1);

    lcd.print("Date: ");
    for (int i = 6; i < 14; i++) {
        inputData[i] = getInputNumber();

        if (i == 8 || i == 10) {
            lcd.print("/");
        }

        lcd.print(inputData[i]);
    }

    // Here compute
    int hour = 10 * inputData[0] + inputData[1];
    int minute = 10 * inputData[2] + inputData[3];
    int second = 10 * inputData[4] + inputData[5];
    int day = 10 * inputData[6] + inputData[7];
    int month = 10 * inputData[8] + inputData[9];
    int year = 1000 * inputData[10] + 100 * inputData[11] + 10 * inputData[12] + inputData[13];

    //check if accettable
    if (hour > 24 or minute > 60 or second > 60 or day > 31 or month > 12) {
        cursorReset(0);
        lcd.print("ERROR!! Input is");
        cursorReset(1);
        lcd.print("not accettable! ");
        delay(2000);
        return;
    }

    // Confirm?
    delay(2000);
    cursorReset(0);
    lcd.print("Up to ok down to");
    cursorReset(1);
    lcd.print("    discard     ");

    while (true) {
        ir.resume();
        delay(200);
        bool decode = ir.decode(&irResult);
        if (decode) {
            // accept UP
            if (isSignalInArray(signalsUP, irResult.value)) {
                setInsertTime(hour - nowDstOffset(), minute, second, day, month, year);
                cursorReset(0);
                lcd.print("     Done...    ");
                delay(500);
                cursorReset(1);
                lcd.print("      :-)       ");
                delay(1000);
                return;
            }
            // refuse DOWN
            else if (isSignalInArray(signalsDOWN, irResult.value)) {
                return;
            }
        }
    }
}

int getInputNumber() {
    while (true) {
        ir.resume();
        delay(200);
        bool decode = ir.decode(&irResult);

        if (decode) {
            if (isSignalInArray(signalsZERO, irResult.value))
                return 0;
            else if (isSignalInArray(signalsONE, irResult.value))
                return 1;
            else if (isSignalInArray(signalsTWO, irResult.value))
                return 2;
            else if (isSignalInArray(signalsTHREE, irResult.value))
                return 3;
            else if (isSignalInArray(signalsFOUR, irResult.value))
                return 4;
            else if (isSignalInArray(signalsFIVE, irResult.value))
                return 5;
            else if (isSignalInArray(signalsSIX, irResult.value))
                return 6;
            else if (isSignalInArray(signalsSEVEN, irResult.value))
                return 7;
            else if (isSignalInArray(signalsEIGHT, irResult.value))
                return 8;
            else if (isSignalInArray(signalsNINE, irResult.value))
                return 9;

        }
    }

}

