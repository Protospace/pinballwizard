/*****************************************************************************
  2023-01-11/12 Tim Gopaul  Removed getHexLineFromSerialPort.. use the main parsing code get command
  2023-01-11 Tim Gopaul added call to make commands case insensitive.
  2023-01-08 Tim Gopaul - add load command to read in Hex format string and place in RAM memory
  2023-01-07 Tim Gopaul - Intel Hex format saveMemory to console
              - have serial run at 1Mhz
  2022-12-30 Tim Gopaul changed the readNumber to read inputs in base 16
  return atoi(numTextPtr);                        //K&R string.h  pg. 251
  return int(strtol(numTextPtr, NULL, 0));        //https://stackoverflow.com/questions/10156409/convert-hex-string-char-to-int
                                                  //strlol string to long will accept 0x for Hex, leading zero for octal, 0b for binary
                                                  // strtol returns a long integer so shorten to int ..or byte.
  
  How to Use CommandLine:
    Create a sketch.  Look below for a sample setup and main loop code and copy and paste it in into the new sketch.

   Create a new tab.  (Use the drop down menu (little triangle) on the far right of the Arduino Editor.
   Name the tab CommandLine.h
   Paste this file into it.

  Test:
     Download the sketch you just created to your Arduino as usual and open the Serial Window.  Type these commands followed by return:
      add 5, 10
      subtract 10, 5

    Look at the add and subtract commands included and then write your own!


*****************************************************************************
  Here's what's going on under the covers
*****************************************************************************
  Simple and Clear Command Line Interpreter

     This file will allow you to type commands into the Serial Window like,
        add 23,599
        blink 5
        playSong Yesterday

     to your sketch running on the Arduino and execute them.

     Implementation note:  This will use C strings as opposed to String Objects based on the assumption that if you need a commandLine interpreter,
     you are probably short on space too and the String object tends to be space inefficient.

   1)  Simple Protocol
         Commands are words and numbers either space or comma spearated
         The first word is the command, each additional word is an argument
         "\n" terminates each command

   2)  Using the C library routine strtok:
       A command is a word separated by spaces or commas.  A word separated by certain characters (like space or comma) is called a token.
       To get tokens one by one, I use the C lib routing strtok (part of C stdlib.h see below how to include it).
           It's part of C language library <string.h> which you can look up online.  Basically you:
              1) pass it a string (and the delimeters you use, i.e. space and comman) and it will return the first token from the string
              2) on subsequent calls, pass it NULL (instead of the string ptr) and it will continue where it left off with the initial string.
        I've written a couple of basic helper routines:
            readNumber: uses strtok and atoi (atoi: ascii to int, again part of C stdlib.h) to return an integer.
              Note that atoi returns an int and if you are using 1 byte ints like uint8_t you'll have to get the lowByte().
            readWord: returns a ptr to a text word
   2022-12-28 Tim Gopaul - return atoi(numTextPtr);//K&R string.h  pg. 251, ia replaced with strtol string to long that allows selection of base 16
                         - return int(strtol(numTextPtr, NULL, 16));  
   4)  DoMyCommand: A list of if-then-elses for each command.  You could make this a case statement if all commands were a single char.
      Using a word is more readable.
          For the purposes of this example we have:
              Add
              Subtract
              nullCommand

  2022-10-18  added commands
              read
              write
              dump
              dumpBuffer
              fill
*/
/******************sample main loop code ************************************

  #include "CommandLine.h"

  void
  setup() {
  Serial.begin(115200);
  }

  void
  loop() {
  bool received = getCommandLineFromSerialPort(CommandLine);      //global CommandLine is defined in CommandLine.h
  if (received) DoMyCommand(CommandLine);
  }

**********************************************************************************/

//Name this tab: CommandLine.h

#include <string.h>
#include <stdlib.h>
#include <errno.h>  // https://stackoverflow.com/questions/26080829/detecting-strtol-failure when strtol fails it returns zero and an errno
#include <limits.h> // used to find LONG_MIN and LONG_MAX



//Function Prototypes from .ino file
void writeAddress(unsigned int address, byte dataByte);

byte readAddress(unsigned int address);
void fillRange(unsigned int addrStart, unsigned int addrCount, byte dataByte);
void fillRandomRange(unsigned int addrStart, unsigned int addrCount, byte dataByte);
void dumpRange(unsigned int addrStart, unsigned int addrCount);
void gameDumpRange(unsigned int addrStart, unsigned int addrCount);
void dumpBuffRange(unsigned int addrStart, unsigned int addrCount);
void saveMemory(unsigned int addrStart, unsigned int addrCount);
void testMemory(unsigned int addrStart, unsigned int addrCount);
void loadMemory();
int helpText();
void testMemory(unsigned int addrStart, unsigned int addrCount, int testLoops);

void gameWriteAddress(unsigned int address, byte dataByte);
byte gameReadAddress(unsigned int address);
void gameDumpRange(unsigned int addrStart, unsigned int addrCount);

//this following macro is good for debugging, e.g.  print2("myVar= ", myVar);
#define print2(x,y) (Serial.print(x), Serial.println(y))


#define CR '\r'
#define LF '\n'
#define BS '\b'
#define NULLCHAR '\0'
#define SPACE ' '
#define ESC 'Q'

#define COMMAND_BUFFER_LENGTH        60                        //length of Serial buffer for incoming commands
char   CommandLine[COMMAND_BUFFER_LENGTH + 1];                 //Read commands into this buffer from Serial.  +1 in length for a termination char

const char *delimiters            = ", \n";                    //commands can be separated by return, space or comma

/*************************************************************************************************************
     your Command Names Here
*/
const char *addCommandToken       = "add";    //Modify here
const char *subtractCommandToken  = "sub";    //Modify here
const char *readCommandToken      = "read";   // read address ignore
const char *writeCommandToken     = "write";  // write address byte
const char *dumpCommandToken      = "dump";   // Dumps memory from starting address with byte count
const char *dumpBuffCommandToken  = "dumpbuffer";   // Dumps memory held in the buffer
const char *fillCommandToken      = "fill";    // Fills the RAM starting at address with byte
const char *fillRandomCommandToken       = "fillrandom";    // Fills with random byte the RAM starting at address with byte
const char *saveMemoryCommandToken       = "save";    // creates Intel Hex output from ram range.
const char *helpCommandToken       = "help";
const char *loadMemoryCommandToken       = "load";    // takes an Intel Hex formatted line and writes it to RAM
const char *testMemoryCommandToken       = "testmemory";    // destructive test read from memory rotate bits and write then compare

const char *gameReadCommandToken      = "gameread";   // read address ignore
const char *gameWriteCommandToken     = "gamewrite";  // write address byte
const char *gameDumpCommandToken  = "gameDump"; // Dumps game memory from starting address with byte count

/*************************************************************************************************************
    getCommandLineFromSerialPort()
      Return the string of the next command. Commands are delimited by return"
      Handle BackSpace character
      Make all chars lowercase
*************************************************************************************************************/

bool
getCommandLineFromSerialPort(char * commandLine)
{
  static uint8_t charsRead = 0;                      //note: COMAND_BUFFER_LENGTH must be less than 255 chars long
  //read asynchronously until full command input
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case CR:      //likely have full command in buffer now, commands are terminated by CR and/or LF
      case LF:
        commandLine[charsRead] = NULLCHAR;       //null terminate our command char array
        if (charsRead > 0)  {
          charsRead = 0;                           //charsRead is static, so have to reset
         // Serial.println(commandLine);
          return true;
        }
        break;
      case BS:                                    // handle backspace in input: put a space in last char
        if (charsRead > 0) {                        //and adjust commandLine and charsRead
          commandLine[--charsRead] = NULLCHAR;
          Serial.print(" \b"); 
        }
        break;
      case ESC:                                    // ESC escape should clear the command line and start over without execiting commad
        if (charsRead > 0) {
          charsRead = 0;                       
          commandLine[charsRead] = NULLCHAR;
          Serial.print(" ESC\n");
          inputMode = CommandMode;
          while(Serial.available() > 0) Serial.read(); //eat what's left comming in.
        }
        break;
        
      default:
        //c = tolower(c);                         //switches all characters to lower case.( not needed switched to strcasecmp()
        if (charsRead < COMMAND_BUFFER_LENGTH) {  //if the buffer is not full add the c charcter read
          commandLine[charsRead++] = c;           //add the character and increment the buffer count charsRead
        }
        commandLine[charsRead] = NULLCHAR;        //the buffer has a NULLCHAR waiting to be overwritten.
        break;
    }
  }
  return false;
}

/* ****************************
   readNumber: return a 16bit (for Arduino Uno) signed integer from the command line
   readWord: get a text word from the command line

*/
int readNumber () {
  char *numTextPtr = strtok(NULL, delimiters);     //K&R string.h  pg. 250 Continue parsing the first call of strtok
  char *endptr = NULL; 
  long int number = 0;
 
  // reset errno to 0 before call
  errno = 0;
  // call to strtol assigning return to number, strtol returns a long integer so shorten to int ..or byte.
                                                   
  number = strtol(numTextPtr, &endptr, 0);          //strlol string to long will accept 0x for Hex, leading zero for octal, 0b for binary
  
//   {
//    Serial.printf("errno: %d\n", errno); 
//  
//    /* test return to number and errno values */
//    if (numTextPtr == endptr)
//        Serial.printf (" number : %lu  invalid  (no digits found, 0 returned)\n", number);
//    else if (errno == ERANGE && number == LONG_MIN)
//        Serial.printf (" number : %lu  invalid  (underflow occurred)\n", number);
//    else if (errno == ERANGE && number == LONG_MAX)
//        Serial.printf (" number : %lu  invalid  (overflow occurred)\n", number);
//    else if (errno == EINVAL)  /* not in all c99 implementations - gcc OK */
//        printf (" number : %lu  invalid  (base contains unsupported value)\n", number);
//    else if (errno != 0 && number == 0)
//        Serial.printf (" number : %lu  invalid  (unspecified error occurred)\n", number);
//    else if (errno == 0 && numTextPtr && !*endptr)
//        Serial.printf (" number : %lu    valid  (and represents all characters read)\n", number);
//    else if (errno == 0 && numTextPtr && *endptr != 0)
//        Serial.printf (" number : %lu    valid  (but additional characters remain)\n", number);
//    }
  
  return int(number);
  }


// ***** readWord *****
char * readWord() {
  char * word = strtok(NULL, delimiters);               //K&R string.h  pg. 250
  return word;
}

void
nullCommand(char * ptrToCommandName) {
  print2("Command not found: ", ptrToCommandName);      //see above for macro print2
}


/****************************************************
   Add your commands here
*/

int addCommand() {                                      //Modify here
  int firstOperand = readNumber();
  int secondOperand = readNumber();
  return firstOperand + secondOperand;
}

int subtractCommand() {                                //Modify here
  int firstOperand = readNumber();
  int secondOperand = readNumber();
  return firstOperand - secondOperand;
}

int readCommand() {                                      //read a byte from RAM
  int address = readNumber();
  byte dataByte = readAddress(address);
  Serial.printf("0x%04X: 0x%02X\n", address, dataByte);
  return dataByte; //return the byte but printing is done here
}

int writeCommand() {                                      //write a byte to RAM
  int address = readNumber();
  int dataByte = readNumber();
  //read before writing
  Serial.printf("0x%04X: 0x%02X\n", address, readAddress(address));
  writeAddress(address, dataByte);
  Serial.printf("0x%04X: 0x%02X\n", address, dataByte);
  return dataByte; //return the byte but printing is done here
}

int dumpCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();

    dumpRange(addrStart, addrCount);
  
  return addrStart;
}

int dumpBuffCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();

    dumpBuffRange(addrStart, addrCount);
  
  return addrStart;
}

int fillCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();
  byte dataByte = readNumber();

    fillRange(addrStart, addrCount, dataByte);
  
  return dataByte;
}

int fillRandomCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();
  byte dataByte = (byte)random(0x100);

    fillRandomRange(addrStart, addrCount, dataByte); //dataByte is recreated for each address of range
 
  return dataByte;
}

// ***** saveMemoryCommand *****
int saveMemoryCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();

  saveMemory(addrStart, addrCount); 
  return 0;
}

// ***** loadMemoryCommand *****
int loadMemoryCommand() {
  loadMemory(); 
  return 0;
}

// ***** Help Text *****
int helpCommand() {
  helpText(); 
 return 0;
}

int gameReadCommand() {                                      //read a byte from RAM
  int address = readNumber();
  byte dataByte = gameReadAddress(address);
  Serial.printf("0x%04X: 0x%02X\n", address, dataByte);
  return dataByte; //return the byte but printing is done here
}

int gameWriteCommand() {                                      //write a byte to RAM
  int address = readNumber();
  int dataByte = readNumber();
  //read before writing
  Serial.printf("0x%04X: 0x%02X\n", address, readAddress(address));
  gameWriteAddress(address, dataByte);
  Serial.printf("0x%04X: 0x%02X\n", address, dataByte);
  return dataByte; //return the byte but printing is done here
}

int gameDumpCommand() {
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();

    gameDumpRange(addrStart, addrCount);
  
  return addrStart;
}


// ***** testMemmory *****
int testMemoryCommand(){
  unsigned int addrStart = readNumber();
  unsigned int addrCount = readNumber();
  unsigned int testLoops = readNumber();
  Serial.println(addrCount);
  
  testMemory(addrStart, addrCount, testLoops);
  return 0;
}
  
/****************************************************
   DoMyHexLine
*/
bool
DoMyHexLine(char * HexLine) {

//  char * endOfFile = ":00000001FF";   //For Intel Hex file transfer a the final line must match.. to switch to command inputMode


  Serial.printf("%s\n", HexLine);

  int HexLineLength = strlen(HexLine);
//  Serial.printf("> HexLineLength: 0x%02X\n", HexLineLength);
  if (HexLineLength < 11){
    Serial.println("> HexLine minimum valid line length is 11 characters");
  }
  if (HexLine[0]!=':') {
    Serial.println("> HexLine must start with : character");
    }
  if (HexLineLength % 2 == 0) {
    Serial.println("> HexLine must be odd when including colon start character");
  }

  #define HEXLINEBYTESSIZE MAXHEXLINE *2 + 5
  byte HexLineBytes[HEXLINEBYTESSIZE];    //Buffer will hold the hex values for input record
  int HLBIndex = 0;
  int checkSum = 0;

  for (int i= 1; i< HexLineLength; i+=2){
    char inByteText[3] = { HexLine[i], HexLine[i+1] , NULLCHAR };
    byte inByte = (byte)strtol(inByteText,NULL,16);
    HexLineBytes[HLBIndex++] = inByte;            //Fill the HexLineBytes buffer with Byte values of HEX input
//    Serial.printf( "%s 0x%02X ", inByteText, inByte);
    checkSum += inByte;
    checkSum &= 0xFF;
//    Serial.printf( " 0x%02X 0x%02X 0x%02X \n", HexLine[i], HexLine[i+1], checkSum);
  }
  if (checkSum != 0 ) Serial.println("> Bad CheckSum");

//Write the bytes to RAM
//HexLineBytes holds all the byte values from the input HEX ASCII pull off addrCount and addrStart
//And save them to the dual port RAM afer checks
//HEX record format  :llaaaatt[dd...]cc

  unsigned int hexCount = HexLineBytes[0];     //if there is junk in these buffers the checkSum will prevent use
  unsigned int addrStart = ((HexLineBytes[1]<<8) + HexLineBytes[2]); 
  int recordType = HexLineBytes[3];             //Record Type 00 for data 01 for end of file
// HLBIndex =4;                                  //Data starts at index 4 in HexLineBytes
  
  if ((checkSum == 0) && (HexLineBytes[0] > 0) && 
     (HexLineBytes[0] < HEXLINEBYTESSIZE) && (recordType == 0)){    
    int address = addrStart;
    HLBIndex =4;                                 //Data starts at index 4 in HexLineBytes
    for(unsigned int i = 0; i < hexCount; i++){
      writeAddress( address++, HexLineBytes[HLBIndex++]);
    } 
  }

  Serial.printf("\n> HexLine %s\n", HexLine);
  
  if (strcasecmp(HexLine, ":00000001FF") == 0){  //use strcasecmp for case insensitive compare
    inputMode = CommandMode;
    Serial.println("> Enter Command"); 
  }
  else{
    Serial.println("> Send next Hex record. To terminate: :00000001FF");
  }
  return(true);
}

/****************************************************
   DoMyCommand
*/
bool
DoMyCommand(char * commandLine) {
 // print2("\nCommand: ", commandLine);
  int result;

  char * ptrToCommandName = strtok(commandLine, delimiters);  //on first call to strtok pass it a pointer to a string, on subsequent calls it NULL to continue parsing
   //   print2("commandName= ", ptrToCommandName);

  if (strcasecmp(ptrToCommandName, addCommandToken) == 0) {                   //Modify here
    result = addCommand();
    Serial.printf(">    The sum is = %d 0x%04X\n", result, result);
  } 
  else if (strcasecmp(ptrToCommandName, subtractCommandToken) == 0) {           //Modify here
      result = subtractCommand();                                       
      Serial.printf(">    The difference is = %d 0x%04X\n", result, result);
  }
  else if (strcasecmp(ptrToCommandName, readCommandToken) == 0) {           //Modify here
      result = readCommand();                                       
  }
  else if (strcasecmp(ptrToCommandName, writeCommandToken) == 0) {           //Modify here
      result = writeCommand();                                       

  } 
  else if (strcasecmp(ptrToCommandName, dumpCommandToken) == 0) {           //Modify here
      result = dumpCommand();                                       
   //   Serial.println();

  }

  else if (strcasecmp(ptrToCommandName, gameReadCommandToken) == 0) {           //Modify here
      result = gameReadCommand();                                       
  }
  else if (strcasecmp(ptrToCommandName, gameWriteCommandToken) == 0) {           //Modify here
      result = gameWriteCommand();                                       

  } 
  else if (strcasecmp(ptrToCommandName, gameDumpCommandToken) == 0) {           //Modify here
      result = gameDumpCommand();                                       
   //   Serial.println();

  }
  else if (strcasecmp(ptrToCommandName, dumpBuffCommandToken) == 0) {           //Modify here
      result = dumpBuffCommand();                                       
      Serial.println();

  }
  else if (strcasecmp(ptrToCommandName, fillCommandToken) == 0) {           //Modify here
      result = fillCommand();                                       
      Serial.println();
  } 
  else if (strcasecmp(ptrToCommandName, fillRandomCommandToken) == 0) {           //Modify here
      result = fillRandomCommand();                                       
      Serial.println();
  } 
  else if (strcasecmp(ptrToCommandName, saveMemoryCommandToken) == 0) {           //Modify here
      result = saveMemoryCommand();                                       
      Serial.println();
  }
  else if (strcasecmp(ptrToCommandName, loadMemoryCommandToken) == 0) {           //Modify here
      result = loadMemoryCommand();                                       
  }

  else if (strcasecmp(ptrToCommandName, helpCommandToken) == 0) {           //Modify here
      result = helpText();                                       
  }
  
  else if (strcasecmp(ptrToCommandName, testMemoryCommandToken) == 0) {           //Modify here
      result = testMemoryCommand();                                       
  }
  else {
      nullCommand(ptrToCommandName);
    }
  return true;
  }

  
