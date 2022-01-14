/*
  TOF_aim:  determine position in an 8x8 matrix to aim animatronic eyes.

  The basic idea is to calibrate the room in setup() by storing an array of
  8x8 ranging values.  Then in loop():

  1.  read out an array of measured values.
  2.  compare each value to the calibration.  Ignore if close to the
    calibration value at that location.  Otherwise, save the position.
  3.  Compare each new position to save with any reviously saved position
    and save only the position of the nearest (to the sensor) value.
  4.   Serial display the results to see how well the algorithm performs.
    Possibility to play with sharpening, etc. to improve the performance.

  This firmware is based upon the example 1 code in the Sparkfun library.    
  
  Author: Bob Glicksman
  Date: 1/7/22
    
  rev 0.6   Adds second data table to continuous output
  rev 0.5.  Added code to overwrite data table
  rev 0.4.  Determine minimum valid range value and the coordinates of that value
    (note: unsure of coordinate system origin - 0,0?)
  rev 0.3.  Filter out bad measurements based upon target_status info
  rev 0.2.  Calibrate background and eliminate background measurements
  rev 0.1.  Restructure example code to put more stuff into functions.

*/

#include <Wire.h>

#include <SparkFun_VL53L5CX_Library.h> //http://librarymanager/All#SparkFun_VL53L5CX

#include <Adafruit_PWMServoDriver.h>
#include <eyeservosettings.h>

Adafruit_PWMServoDriver pwm_; 
// Servo Numbers for the Servo Driver board
#define X_SERVO 0
#define Y_SERVO 1
#define L_UPPERLID_SERVO 2
#define L_LOWERLID_SERVO 3
#define R_UPPERLID_SERVO 4
#define R_LOWERLID_SERVO 5


// used in main loop to move cursor to the top of the displayed table
#define CURSOR_RESET_ROWS 22

// use D7 LED for status
const int LED_PIN = D7;

// noise range in measured data.  Anything within +/- 50 of the calibrations is noise
const uint16_t NOISE_RANGE = 50;
const uint16_t MAX_CALIBRATION = 2000;  // anything greater is set to 2000 mm

SparkFun_VL53L5CX myImager;
VL53L5CX_ResultsData measurementData; // Result data class structure, 1356 byes of RAM

// declare 8x8 array of calibration values
int32_t calibration[64];

int imageResolution = 0; // read this back from the sensor
int imageWidth = 0; // read this back from the sensor


void setup()
{
  // turn on D7 LED to indicate that we are in setup()
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  Serial.begin(115200);
  delay(1000);
  moveTerminalCursorDown(CURSOR_RESET_ROWS);
  Serial.println("SparkFun VL53L5CX Imager Example");

  Wire.begin(); //This resets to 100kHz I2C
  Wire.setClock(400000); //Sensor has max I2C freq of 400kHz 
  
  Serial.println("Initializing sensor board. This can take up to 10s. Please wait.");
  if (myImager.begin() == false)
  {
    Serial.println(F("Sensor not found - check your wiring. Freezing"));
    while (1) ;
  }
  
  myImager.setResolution(64); //Enable all 64 pads - 8 x 8 array of readings
  
  imageResolution = myImager.getResolution(); //Query sensor for current resolution - either 4x4 or 8x8
  imageWidth = sqrt(imageResolution); //Calculate printing width

  // debug print statement - are we communicating with the module
  String theResolution = "Resolution = ";
  theResolution += String(imageResolution);
  Serial.println(theResolution);

  myImager.startRanging();

  // fill in the calibration data array

  // wait for data to be ready
  do {
    // do nothing here, wait for data to be ready
    delay(5); //Small delay between polling
  } while(myImager.isDataReady() != true);

  // data is now ready
  if (myImager.getRangingData(&measurementData)) //Read distance data into array
  {
    // read out the measured data into an array
    for(int i = 0; i < 64; i++)
    {
      calibration[i] = measurementData.distance_mm[i];

      // adjust for calibration values being 0 or too long for measurement
      if( (calibration[i] == 0) || (calibration[i] > MAX_CALIBRATION) ) {
        calibration[i] = MAX_CALIBRATION;
      }
    }
    Serial.println("Calibration data:");
    prettyPrint(calibration);
    Serial.println("End of calibration data\n");
  }
    
    // set up eyes and have the lids open
    pwm_ = Adafruit_PWMServoDriver();
    pwm_.begin(); 
    pwm_.setPWMFreq(60);  // Analog servos run at ~60 Hz updates
    pwm_.setPWM(L_LOWERLID_SERVO, 0, LEFT_LOWER_OPEN);
    pwm_.setPWM(R_LOWERLID_SERVO, 0, RIGHT_LOWER_OPEN);
    pwm_.setPWM(L_UPPERLID_SERVO, 0, LEFT_UPPER_OPEN);
    pwm_.setPWM(R_UPPERLID_SERVO, 0, RIGHT_UPPER_OPEN);
    moveEyes(0,0);
    delay(1000);
    moveEyes(50,50);
    delay(1000);
    moveEyes(100,100);
    delay (1000);


  // indicate that setup() is complete
  digitalWrite(LED_PIN, LOW);
}

void loop()
{
  int32_t measuredData, temp, smallestValue, focusX, focusY;
  uint8_t statusCode;
  int32_t adjustedData[imageResolution];
  int32_t secondTable[imageResolution];   // second table to print out
  String secondTableTitle = ""; // will hold title of second table 
  
  //Poll sensor for new data.  Adjust if close to calibration value
  
  if (myImager.isDataReady() == true)
  {
    if (myImager.getRangingData(&measurementData)) //Read distance data into ST driver array
    {
      // initialize findings
      smallestValue = MAX_CALIBRATION; // start with the max allowed
      focusX = -255;  // code for no focus determined
      focusY = -255;  // code for no focus determined

      // process the measured data
      for(int i = 0; i < imageResolution; i++) 
      {
        // process the status code, only good data if status code is 5 or 9
        statusCode = measurementData.target_status[i];
        measuredData = measurementData.distance_mm[i];
        secondTable[i] = measurementData.nb_target_detected[i];
        secondTableTitle = "num targets";

        if( (statusCode != 5) && (statusCode != 9)) { // TOF measurement is bad
          adjustedData[i] = -1;

        } else if ( (measuredData == 0) || (measuredData > MAX_CALIBRATION) ) 
        { //data out of range
            
          adjustedData[i] = -2;  // indicate out of range data

        } else 
        { // data is good and in range, check if background
          
          // check new data against calibration value
          temp = measuredData - calibration[i];
          
          // take the absolute value
          if(temp < 0) {
            temp = -temp;
          }

          if(temp <= NOISE_RANGE) 
          { // zero out noise  
              
            adjustedData[i] = -3; // data is background; ignore
          } 
          else 
          {
            adjustedData[i] = (int16_t) measuredData;
          }

        }
        if( (adjustedData[i] > 0) && (adjustedData[i] < smallestValue) ) {
          // we have a new smallest range that is not calibration; record
          smallestValue = adjustedData[i];
          focusX = i % imageWidth;
          focusY = i / imageWidth;
        }
      
      } 
      prettyPrint(adjustedData); 

      // print out focus value found
      Serial.print("\nFocus on x = ");
      Serial.print(focusX);
      Serial.print(" ;y = ");
      Serial.print(focusY);
      Serial.print(" range = ");
      Serial.println(smallestValue);
      Serial.println();
      Serial.println();

      Serial.println(secondTableTitle);
      prettyPrint(secondTable);
      Serial.println();

      // XXX overwrite the previous display
      moveTerminalCursorUp(CURSOR_RESET_ROWS);

        //decide where to point the eyes
        // x,y 0-100
        moveEyes(focusX * 10 ,focusY * 10);

    }
  }
  delay(5); //Small delay between polling




//  delay(3000);  // longer delay to ponder results
}

// function to pretty print data to serial port
void prettyPrint(int32_t dataArray[]) {
  //The ST library returns the data transposed from zone mapping shown in datasheet
  //Pretty-print data with increasing y, decreasing x to reflect reality 

  for(int y = 0; y <= imageWidth * (imageWidth - 1) ; y += imageWidth)  {

    for (int x = imageWidth - 1 ; x >= 0 ; x--) {
      Serial.print("\t");

      // XXXX changed to format print
      Serial.printf("%-5d", dataArray[x + y]);
    }
    Serial.println();

  } 
  // Serial.println();
}

// function to move the terminal cursor back up to overwrite previous data printout
void moveTerminalCursorUp(int numlines) {
  String cursorUp = String("\033[") + String(numlines) + String("A");
  Serial.print(cursorUp);
  Serial.print("\r");
}

// function to move the terminal cursor down to get past previous data printout - used on startup
void moveTerminalCursorDown(int numlines) {
  String cursorUp = String("\033[") + String(numlines) + String("B");
  Serial.print(cursorUp);
  Serial.print("\r");
}

void moveEyes (int x, int y){

    double xPos = map(x, 0, 100, X_POS_MID + X_POS_LEFT_OFFSET, X_POS_MID + X_POS_RIGHT_OFFSET);
    double yPos = map(y, 0, 100, Y_POS_MID + Y_POS_DOWN_OFFSET, Y_POS_MID + Y_POS_UP_OFFSET);

    pwm_.setPWM(X_SERVO, 0, xPos);
    pwm_.setPWM(Y_SERVO, 0, yPos);   

}