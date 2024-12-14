#include <Keypad.h>
#include <ArducamSSD1306.h>     // Modification of Adafruit_SSD1306 for ESP8266 compatibility
#include <Adafruit_GFX.h>       // Needs a little change in original Adafruit library (See README.txt file)
#include <Wire.h>               // For I2C comm, but needed for not getting compile error
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SD.h>
#include <CSV_Parser.h>
#include <SPI.h>
#include <avr/io.h>
#include <avr/interrupt.h>

const byte ROWS = 4;            // Four rows
const byte COLS = 4;            // Four columns
const int updateRate = 1000;    // Hz
const double pi = 3.1415926535;
const double maxVoltage = 10;
const int chipSelect = 53;
const char * f_name = "/Waveform.csv";

#define OLED_RESET  16  // Pin 15 -RESET digital signal
#define LOGO16_GLCD_HEIGHT 16
#define LOGO16_GLCD_WIDTH  16

// Defining GPIO Pins associated with parallel inputs 
// to the DAC08.
#define bitOne 29
#define bitTwo 28
#define bitThree 27
#define bitFour 26
#define bitFive 25
#define bitSix 24
#define bitSeven 23
#define bitEight 22

// Defining the character matrix for the keyboard
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {9, 8, 7, 6}; //connect to the row pinouts of the keypad
byte colPins[COLS] = {5, 4, 3, 2}; //connect to the column pinouts of the keypad

// Defining a global index variable for use in the interrupt function
int ii = 0;

// Create an object of keypad
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// Creating a file object to read from:
File file;

// Creating an object of the display
ArducamSSD1306 display(OLED_RESET); // FOR I2C

// Defining global variables for the waveform to be output
int amplitudeInput = 0;
int waveTypeInput = 0;
int waveVector[updateRate];

void setup() {
  // Initializing Serial monitor and bluetooth serial transmission
  Serial.begin(9600);
  Serial1.begin(9600);

  // Initializing Display
  display.begin();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(20,20);
  display.display();

  // Initializing DAC Pins
  pinMode(bitOne, OUTPUT);
  pinMode(bitTwo, OUTPUT);
  pinMode(bitThree, OUTPUT);
  pinMode(bitFour, OUTPUT);
  pinMode(bitFive, OUTPUT);
  pinMode(bitSix, OUTPUT);
  pinMode(bitSeven, OUTPUT);
  pinMode(bitEight, OUTPUT);

  digitalWrite(bitOne, LOW);
  digitalWrite(bitTwo, LOW);
  digitalWrite(bitThree, LOW);
  digitalWrite(bitFour, LOW);
  digitalWrite(bitFive, LOW);
  digitalWrite(bitSix, LOW);
  digitalWrite(bitSeven, LOW);
  digitalWrite(bitEight, LOW);

  // Configuring Timer Interrupt
  cli();         // disable global interrupts

  // Configuring clock variables, clock is set to no prescaler
  TCCR3A = 0;
  TCCR3B = 0;
  TIMSK3 |= (1 << TOIE1);
  TCCR3B |= (1 << CS10); 
  
  // enable global interrupts: 
  sei();
}
  
void loop() {

  /*
      Getting whether the user wants to
      import from file or choose waveform
      via UI.
  */

  // Flags for passing through GUI stages
  int fromFile = 0;
  int selected = 0;

  fileOrInputScreen();

  char key = "";

  // While "A" or "B" has not been selected, keep polling
  while(!selected) {

    key = keypad.getKey();

    if((int) key == 65) {
      fromFile = 0;
      selected = 1;
    }
    if((int) key == 66) {
      fromFile = 1;
      selected = 1;
    }
  }


  /*
      Case where user wants to input
      parameters for wave from the UI.

      While loop iteratively gathers what key
      was pressed, then updates selection screen
      to selected parameters. While loop is exited when
      the key pressed is "#", which flips the "selected"
      flag.
  */

  if(!fromFile) {

    selected = 0;
    updateSelectionScreen(0, 0);

    key = "";
    key = keypad.getKey();

    // While the "#" Key has not been pressed
    while(!selected) {
      key = keypad.getKey();

      // If the key pressed is 0-9
      if((int) key == 48 || (int) key == 49 || (int) key == 50 || (int) key == 51 || (int) key == 52 || 
        (int) key == 53 || (int) key == 54 || (int) key == 55 || (int) key == 56 || (int) key == 57) {

        amplitudeInput = (int) key - 48;

      // If key pressed is an A (sine wave)
      } else if((int) key == 65) {
        waveTypeInput = 0;

      // If key pressed is an B (Square wave)    
      } else if((int) key == 66) {
        waveTypeInput = 1;

      // If key pressed is an C (Sawtooth Wave)
      } else if((int) key == 67) {
        waveTypeInput = 2;

      // If key pressed is a D (Triangle Wave)
      } else if((int) key == 68) {
        waveTypeInput = 3;

      // If the # key is pressed, exits while loop.
      } else if((int) key == 35) {
        selected = 1;
      }

      // Update the user IO screen
      updateSelectionScreen(amplitudeInput, waveTypeInput);

    }

    // Create the vector of inputted results
    createWaveVector(amplitudeInput, waveTypeInput);


    displayWaveVector();

    // Transmitting waveform information through bluetooth
    String waveTypes[] = {"Sine", "Square", "Sawtooth", "Triangle"};
    Serial1.print("Selected Wavetype: ");
    Serial1.println(waveTypes[waveTypeInput]);
    Serial1.print("Amplitude: ");
    Serial1.println(amplitudeInput);
  }




  /*
      Case where the user wants to import the
      waveform directly from a file stored on the SD
      card in the SD module.

      File must be names "Waveform.csv".

      [Insert description of code here]
  */
  else {

    // Getting waveform from file
    int results = getWaveformFromFile();

    // Transmitting waveform information to bluetooth
    Serial1.println("Waveform gathered from file!");

    displayWaveVector();

  }






  key = "";
  int stopButtonPressed = 0;

  // Continuously polls to see if "#" key is pressed while
  // timer continuously updates DAC pins to output wave.
  while(!stopButtonPressed) {
    key = keypad.getKey();

    if((int) key == 35) {
      stopButtonPressed = 1;
      // Setting waveVector to amplitude of zero if stop
      // button is pressed to stop output waveform.
      createWaveVector(0, 0);
    } else {

      // This is where timer is constantly updating DAC pins

    }
  }
}















/*

                Helper Functions

*/


/*
      updateSelectionScreen(int, int)

      Inputs two ints, one for the amplitude and one for the waveType.
      It will then draw the user interface on the screen displaying
      the inputted selection.
*/
void updateSelectionScreen(int amplitude, int waveType) {

  String waveTypes[] = {"Sine", "Square", "Sawtooth", "Triangle"};

  // Initializing Display Options
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Printing selection screen information
  display.print("Amplitude: ");
  display.println(amplitude);

  for(int i = 0; i < 4; i++) {
    if(i == waveType) {
      display.setTextColor(BLACK, WHITE);
    } else {
      display.setTextColor(WHITE, BLACK);
    }
    display.println(waveTypes[i]);
  }

  display.setTextColor(WHITE, BLACK);
  display.println();
  display.println("Press # to start!");
  display.display();

}







/*
      fileOrInputScreen()

      This funcion solely displays the screen to let
      the user choose between importing from file or 
      choosing from premade waveforms.
*/
void fileOrInputScreen() {

  // Initializing Display Options
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);

  display.println("Press A to proceed to\nselection screen.");
  display.println();
  display.println("Press B to import\nfrom SD card.");

  display.display();

}







/*
      createWaveVector(int, int)

      Inputs two ints, one for the amplitude and one for the waveType.
      This will create an int wave vector between the values of 0 and 255
      that can then be used in the updateDACPins() function to push to the DAC.

      It has four wave types:
      1) Sine wave
      2) Square wave
      3) Triangle wave
      4) Sawtooth wave
*/
void createWaveVector(int amplitude, int waveType) {
  amplitude = double(amplitude);
  double currentVoltage;

  // Generating sine wave
  if(waveType == 0) { 
    for(int i = 0; i < updateRate; i++) {
        currentVoltage = (amplitude/2) + ((amplitude/2) * sin((1 / double(updateRate)) * 2 * pi * double(i)));
        waveVector[i] = int((currentVoltage / maxVoltage) * 256);
    }

  // Generating square wave
  } else if(waveType == 1) {
    for(int i = 0; i < updateRate; i++) {
      if(i < updateRate / 2) {
        currentVoltage = amplitude;
      } else {
        currentVoltage = 0;
      }
      waveVector[i] = int((currentVoltage / maxVoltage) * 256);
    }

  // Generating Sawtooth Wave
  } else if(waveType == 2) {

    // Iterating through elements of waveVector
    for(int i = 0; i < updateRate; i++) {
      
      currentVoltage = amplitude * (((double) i) / ((double) updateRate));

      waveVector[i] = int((currentVoltage / maxVoltage) * 256);
    }

  // Generating Triangle Wave
  } else if(waveType == 3) {
    double j = 0;
    double dUpdateRate = (double) updateRate;

    // Iterating through elements of waveVector
    for(int i = 0; i < updateRate; i++) {
      
      currentVoltage = amplitude * ((j * 2) / dUpdateRate);

      // Decrementing/Incrementing j if in the first/second part of
      // creating the waveform
      if(i < dUpdateRate / 2) {
        j++;
      } else {
        j--;
      }

      waveVector[i] = int((currentVoltage / maxVoltage) * 256);
    }

  } else {
    Serial.println("Error -- In createWaveVector(): Wavetype not supported yet!");
  }
  
}





/*
      updateDACPins()

      Utilizes the global waveVector variable to go through the vector
      and toggle the GPIO pins that are connected to the parallel inputs
      of the DAC. This will output the waveform required at the output of
      the waveform generator.

      ** This function is commented out due to implementation
         of the interrupt function.
*/ /*
void updateDACPins() {

  while(ii < (sizeof(waveVector) / sizeof(waveVector[0]))) {

    if((waveVector[ii] & 1) == 1) {
      digitalWrite(bitOne, HIGH);
    } else {
      digitalWrite(bitOne, LOW);
    }

    if((waveVector[ii] & 2) == 2) {
      digitalWrite(bitTwo, HIGH);
    } else {
      digitalWrite(bitTwo, LOW);
    }

    if((waveVector[ii] & 4) == 4) {
      digitalWrite(bitThree, HIGH);
    } else {
      digitalWrite(bitThree, LOW);
    }

    if((waveVector[ii] & 8) == 8) {
      digitalWrite(bitFour, HIGH);
    } else {
      digitalWrite(bitFour, LOW);
    }

    if((waveVector[ii] & 16) == 16) {
      digitalWrite(bitFive, HIGH);
    } else {
      digitalWrite(bitFive, LOW);
    }

    if((waveVector[ii] & 32) == 32) {
      digitalWrite(bitSix, HIGH);
    } else {
      digitalWrite(bitSix, LOW);
    }

    if((waveVector[ii] & 64) == 64) {
      digitalWrite(bitSeven, HIGH);
    } else {
      digitalWrite(bitSeven, LOW);
    }

    if((waveVector[ii] & 128) == 128) {
      digitalWrite(bitEight, HIGH);
    } else {
      digitalWrite(bitEight, LOW);
    }

    i++;
  }

  i = 0;
}
*/



/*
      getWaveformFromFile()

      Reads from a .csv file on the SD card, "Waveform.csv".
      This .csv file will hold 1000 data points of different voltages
      that you want to create. It stores this to the global variable
      waveVector, and this can then be used with updateDACPins().
*/
int getWaveformFromFile() {
  int results = -1;

  // Initializing the SD card
  if (!SD.begin(chipSelect)) {
    Serial.println("ERROR: Card failed, or not present");
    while (1);
  }
  Serial.println("Card initialized.");

  // Checking if SD Card exists
  if (!SD.exists(f_name)) {
    Serial.println("ERROR: File \"" + String(f_name) + "\" does not exist.");  
    while (1);
  }
  file = SD.open(f_name, FILE_READ);
  if (!file) {
    Serial.println("ERROR: File open failed");
    while (1);
  }

  // Parsing CSv file on SD card
  CSV_Parser cp("f", false);
  int row_index = 0;
  double *values = (double*)cp[0];

  while (cp.parseRow()) {
    results = 1;
    // Here we could use string indexing but it would be much slower.
    // char *email = ((char**)cp["Email"])[0];

    double value = values[0];

    waveVector[row_index] = int((value / maxVoltage) * 256);

    row_index++;
  }

  return(results);
}

// Below are just two helper functions that are only used
// in parsing the .csv file that the user can alter.
char feedRowParser() {
  return file.read();
}
bool rowParserFinished() {
  return ((file.available()>0)?false:true);
}






/*
    This function displays the values displayed
    in the global waveVector variable on the TFT display.

    Since the amount of elements in waveVector is much greater
    than the amount of horizontal pixels on the display, 
    the indexes of waveVector that are displayed are spaced out.
    The calculation that determines this is the round(1000/128).

    Furthermore, the values stored in waveVector are between 0 and 255,
    which is larger than the 64 vertical pixels that the display has.
    Another calculation to scale the elements of waveVector appropriately
    is done.

    Then each pixel is plotted on the display at the x and y coordinates
    determined.
*/
void displayWaveVector() {

  // Displaying axis on display
  display.clearDisplay();

  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("10V");

  display.setCursor(0, 55);
  display.println("0V");

  for(int i = 0; i < 55; i++) {
    display.drawPixel(20, i, WHITE);
  }

  // Displying waveform on display
  for(int i = 20; i < 128; i++) {

    int indexToDisplay = (int) round(1000 / (128 - 20)) * i;

    int x = i;
    int y = 50 - (int) ((50.0/256.0) * ((double) waveVector[indexToDisplay]));

    display.drawPixel(x, y, WHITE);
    display.drawPixel(x - 5, 50, WHITE);
  }

  display.display();
}





/*
    This is an interrupt service routing to update the DAC pins
    whenever they must be updated. Each time this function is called,
    the global index variable ii is incremented until it reaches the
    end of the array.

    The function uses bitwise comparisons with the element of 
    waveVector[ii] to determine if a certain DAC pin should
    be on or off.
*/
ISR(TIMER3_OVF_vect){

  if((waveVector[ii] & 1) == 1) {
      digitalWrite(bitOne, HIGH);
    } else {
      digitalWrite(bitOne, LOW);
    }

    if((waveVector[ii] & 2) == 2) {
      digitalWrite(bitTwo, HIGH);
    } else {
      digitalWrite(bitTwo, LOW);
    }

    if((waveVector[ii] & 4) == 4) {
      digitalWrite(bitThree, HIGH);
    } else {
      digitalWrite(bitThree, LOW);
    }

    if((waveVector[ii] & 8) == 8) {
      digitalWrite(bitFour, HIGH);
    } else {
      digitalWrite(bitFour, LOW);
    }

    if((waveVector[ii] & 16) == 16) {
      digitalWrite(bitFive, HIGH);
    } else {
      digitalWrite(bitFive, LOW);
    }

    if((waveVector[ii] & 32) == 32) {
      digitalWrite(bitSix, HIGH);
    } else {
      digitalWrite(bitSix, LOW);
    }

    if((waveVector[ii] & 64) == 64) {
      digitalWrite(bitSeven, HIGH);
    } else {
      digitalWrite(bitSeven, LOW);
    }

    if((waveVector[ii] & 128) == 128) {
      digitalWrite(bitEight, HIGH);
    } else {
      digitalWrite(bitEight, LOW);
    }

    if(ii < (sizeof(waveVector) / sizeof(waveVector[0]))) {
      ii++;
    } else {
      ii = 0;
    }
}




























