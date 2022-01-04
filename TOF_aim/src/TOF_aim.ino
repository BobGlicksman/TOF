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
  Date: 12/26/21

  rev 0.1.  Restructure example code to put more stuff into functions.

*/

#include <Wire.h>

#include <SparkFun_VL53L5CX_Library.h> //http://librarymanager/All#SparkFun_VL53L5CX

// use D7 LED for status
const int LED_PIN = D7;

// noise range in measured data.  Anything within +/- 50 of the calibrations is noise
const uint16_t NOISE_RANGE = 50;
const uint16_t MAX_CALIBRATION = 2000;  // anything greater is set to 2000 mm

SparkFun_VL53L5CX myImager;
VL53L5CX_ResultsData measurementData; // Result data class structure, 1356 byes of RAM

// XXX 8x8 array of calibration values
uint16_t calibration[64];

int imageResolution = 0; //Used to pretty print output
int imageWidth = 0; //Used to pretty print output

void setup()
{
  // XXX turn on D7 LED to indicate that we are in setup()
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  Serial.begin(115200);
  delay(1000);
  Serial.println("SparkFun VL53L5CX Imager Example");

  Wire.begin(); //This resets to 100kHz I2C
  Wire.setClock(400000); //Sensor has max I2C freq of 400kHz 
  
  Serial.println("Initializing sensor board. This can take up to 10s. Please wait.");
  if (myImager.begin() == false)
  {
    Serial.println(F("Sensor not found - check your wiring. Freezing"));
    while (1) ;
  }
  
  myImager.setResolution(8*8); //Enable all 64 pads
  
  imageResolution = myImager.getResolution(); //Query sensor for current resolution - either 4x4 or 8x8
  imageWidth = sqrt(imageResolution); //Calculate printing width

  // XXX debug print statement - are we communicating with the module
  String theResolution = "Resolution = ";
  theResolution += String(imageResolution);
  Serial.println(theResolution);

  myImager.startRanging();

  // XXX fill in the calibration data array

  // wait for data to be ready
  do {
    // do nothing here, wait for data to be ready
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
  
  // XXX clear out the ST driver array before the next reading
  //clearOutData(measurementData.distance_mm);

  delay(5); //Small delay between polling

  // XXX indicate that setup() is complete
  digitalWrite(LED_PIN, LOW);
}

void loop()
{
  int32_t measuredData, temp;
  uint16_t adjustedData[imageResolution];
  
  //Poll sensor for new data.  Adjust if close to calibration value
  if (myImager.isDataReady() == true)
  {
    if (myImager.getRangingData(&measurementData)) //Read distance data into ST driver array
    {
      // read out the measured data into an array
      for(int i = 0; i < 64; i++)
      {
          // clamp measured values to 2000 mm
          measuredData = measurementData.distance_mm[i];
          if( (measuredData == 0) || (measuredData > MAX_CALIBRATION) ) {
            measuredData = MAX_CALIBRATION;
          }

          // check new data against calibration value
          temp = measuredData - calibration[i];
          
          // take the absolute value
          if(temp < 0) {
            temp = -1 * temp;
          }
          if(temp <= NOISE_RANGE) { // zero out noise  
            adjustedData[i] = MAX_CALIBRATION;
          } 
          else {
            adjustedData[i] = measuredData;
          }
      }
      prettyPrint(adjustedData);

      // XXX print out the target status data to see what info it actually provides
      Serial.println("Target status data: \n");
      for(int i = 0; i < 64; i++)
      {
          measuredData = (uint16_t)measurementData.target_status[i];
          adjustedData[i] = measuredData;
      }
      prettyPrint(adjustedData);
      Serial.println();

    }
  }

  // XXX clear out the ST driver array before the next reading
  //clearOutData(measurementData.distance_mm);

  //XXX try srart and stop ranging
 /* myImager.stopRanging();
  delay(100);
  myImager.startRanging();
  */

  delay(5); //Small delay between polling
}

// function to pretty print data to serial port
void prettyPrint(uint16_t dataArray[]) {
  //The ST library returns the data transposed from zone mapping shown in datasheet
  //Pretty-print data with increasing y, decreasing x to reflect reality 

  for(int y = 0; y <= imageWidth * (imageWidth - 1) ; y += imageWidth)  {

    for (int x = imageWidth - 1 ; x >= 0 ; x--) {
      Serial.print("\t");
      Serial.print(dataArray[x + y]);
    }
    Serial.println();

  } 
  Serial.println();
}

// XXX function to clear out the data array
// XXX clear out the ST driver array before the next reading
void clearOutData(int16_t dataArray[]) {
  for (int i = 0; i < 64; i++) {
      dataArray[i] = 0;
  }
}