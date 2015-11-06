// FeedThing uses the SparkFun thing, a photo interrupter, and an 
// Adafruit v2 motor shield to feed mice and log their eating habits 
// to a phant server.

#include <ESP8266WiFi.h>
#include <Phant.h> // Include the SparkFun Phant library, this is where the data will go

#include <Wire.h>
#include <Adafruit_MotorShield.h> // NOTE: using a modified version of the Adafruit v2 MotorShield library, depends n I2C (Wire)

#define DEBUG_ON 1
#include "debugOutput.h"

// Private information, specific to this project instantiation
#include "wifiPassword.h"
#include "phantKeys.h"

/////////////////////
// Pin Definitions //
/////////////////////
const int LED_PIN = 5; // Thing's onboard, green LED, it is the same line as the BOOST enable
const int ANALOG_PIN = A0; // The only analog pin on the Thing, max input volatage is 1V

const int LOW_BATTERY_INDICATOR_PIN = 4; // Low battery indicator from the boost
const int PHOTO_INTERRUPTER_PIN = 12; // Digital pin to be read
const int BOOST_OUTPUT_PIN = 13; // Turn on the boost power for the motor

/////////////////////////////////////
// Motor setup                     //
// Stepper motor on Adafruit shild //
/////////////////////////////////////
const int MOTOR_STEPS_PER_REVOLUTION = 513;
const int STEPPER_SPEED = 30;
Adafruit_MotorShield gMotorShield = Adafruit_MotorShield();
Adafruit_StepperMotor *gPtrToStepper = gMotorShield.getStepper(MOTOR_STEPS_PER_REVOLUTION,2);

/////////////////////////////////////
// Short functions                 //
/////////////////////////////////////
const char* errorBatteryLowString = "Battery low";
const char* errorDispensorEmptyString = "Dispensor empty";
const char* noErrorTimedWakeup = "We're all fine here.";
const char* noErrorString = "";


/////////////////////////////////////
// Short functions                 //
/////////////////////////////////////
void turnBoostOn()  { digitalWrite(BOOST_OUTPUT_PIN, HIGH); }
void turnBoostOff() { digitalWrite(BOOST_OUTPUT_PIN, LOW);  }

// photo interrupter is high when clear, low when blocked by food
bool noFoodAvailable() { return digitalRead(PHOTO_INTERRUPTER_PIN); }
// low battery indicator is 0 when battery is low
bool batteryIsGood() { return digitalRead(LOW_BATTERY_INDICATOR_PIN); }

void turnMotorOneIncrement() 
{
  const int STEPS_TO_INCREMENT = 64;
  gPtrToStepper->step(STEPS_TO_INCREMENT/2,FORWARD,DOUBLE);
  gPtrToStepper->step(STEPS_TO_INCREMENT,BACKWARD,DOUBLE);
  gPtrToStepper->release();
}

void enterSleep()
{
  // will not return after this, system will reset and start with setup()
  // deepSleep time is defined in microseconds. 
  // Multiply seconds by 1e6 to get microseconds.
  const int sleepTimeS = -1; // oFIXME: sleep doesn't work so sleep forever
  ESP.deepSleep(sleepTimeS * 1000000);
}

void setup() 
{
  initHardware();
  debugOutputLn("HW init complete");
  feedAndPost();
  enterSleep();  
}


void loop() 
{
  // do nothing, should never get here due to sleeping
}

void feedAndPost() 
{
  int batteryIndicator = -1; // unknown
  const char ** errorStringPtr;
  errorStringPtr = &noErrorString;
  
  // The motor can only go so many times before we decide we are out of food
  const unsigned int MAX_NUMBER_MOTOR_INCREMENTS = 10; 
  unsigned int numMotorTurns = 0;
  turnBoostOn();
  while (noFoodAvailable() && numMotorTurns < MAX_NUMBER_MOTOR_INCREMENTS) {
    turnMotorOneIncrement();
    numMotorTurns++;
    debugOutputLn("Turning motor...");
  }

  if (numMotorTurns) {
    if (numMotorTurns >= MAX_NUMBER_MOTOR_INCREMENTS){
      errorStringPtr = &errorDispensorEmptyString;
    } 
  } else { // timed wakeup, nothing really going on
    errorStringPtr = &noErrorTimedWakeup;    
  }
  
  // check battery at the worst part and while boost is still on to give us a value
  debugOutputLn("Checking battery...");
  if (batteryIsGood()) { 
    batteryIndicator = 1;
  } else {
    batteryIndicator = 0;
    errorStringPtr = &errorBatteryLowString;
  }    
  turnBoostOff();

  // turn on WiFi and post to phant here
  connectWiFi();
  debugOutputLn("Connected to WiFi");
  
  waitWhilePostToPhant(numMotorTurns, batteryIndicator, *errorStringPtr);
  debugOutputLn("Posted data");

}

void connectWiFi()
{
  byte ledStatus = LOW;

  // Set WiFi mode to station (as opposed to AP or AP_STA)
  WiFi.mode(WIFI_STA);
  // WiFI.begin([ssid], [passkey]) initiates a WiFI connection
  // to the stated [ssid], using the [passkey] as a WPA, WPA2,
  // or WEP passphrase.
  WiFi.begin(WiFiSSID, WiFiPSK);

  // Use the WiFi.status() function to check if the ESP8266
  // is connected to a WiFi network.
  while (WiFi.status() != WL_CONNECTED)
  {
    // Blink the LED
    digitalWrite(LED_PIN, ledStatus); // Write LED high/low
    ledStatus = (ledStatus == HIGH) ? LOW : HIGH;

    // Delays allow the ESP8266 to perform critical tasks
    // defined outside of the sketch. These tasks include
    // setting up, and maintaining, a WiFi connection.
    delay(100);
    // Potentially infinite loops are generally dangerous.
    // Add delays -- allowing the processor to perform other
    // tasks -- wherever possible.
  }
}

void initHardware()
{
  debugInit(); // turns on serial if DEBUG_ON set to 1

  pinMode(PHOTO_INTERRUPTER_PIN , INPUT);
  pinMode(LOW_BATTERY_INDICATOR_PIN, INPUT);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(BOOST_OUTPUT_PIN, OUTPUT);
  digitalWrite(BOOST_OUTPUT_PIN, LOW);

  // set up stepper
  gMotorShield.begin(); // use default I2C address of 0x40
  gPtrToStepper->setSpeed(STEPPER_SPEED); // rpm, max suggested by Adafruit for this 5V stepper
}

int waitWhilePostToPhant(int numMotorTurns, int batteryIndicator, const char* errorString) 
{
  while (postToPhant(numMotorTurns, batteryIndicator, errorString) != 1)  {
    delay(100);
  }  
}
int postToPhant(int numMotorTurns, int batteryIndicator, const char* errorString)
{
  // LED turns on when we enter, it'll go off when we 
  // successfully post.
  digitalWrite(LED_PIN, HIGH);

  // Declare an object from the Phant library - phant
  Phant phant(PhantHost, PublicKey, PrivateKey);

  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) to "Thing-":
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String postedID = "Thing-" + macID;

  // Add the four field/value pairs defined by our stream:
  phant.add("id", postedID);
  phant.add("motorturns", numMotorTurns);
  phant.add("voltage", batteryIndicator);
  phant.add("warning", errorString);  

  // Now connect to data.sparkfun.com, and post our data:
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(PhantHost, httpPort)) 
  {
    // If we fail to connect, return 0.
    return 0;
  }
  // If we successfully connected, print our Phant post:
  client.print(phant.post());

  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){
    String line = client.readStringUntil('\r');
    //Serial.print(line); // Trying to avoid using serial
  }

  // Before we exit, turn the LED off.
  digitalWrite(LED_PIN, LOW);

  return 1; // Return success
}

