// Protospace is running code version PinBallMemoryPort20230201
// The next version of code starts dated 2023 02 05
// 
// 2023-01-29 Tim Gopaul troubleshoot bad first byte read
// 2023-01-25 Tim Gopaul, added a second CE signal to control two dual port RAMs
//
// If the Atmega is reading or writing an address that is requested by the Pinball 6800 the BusyR line will go low for the pinball machine
//    if the Pinball 6800 is reading.. no problem.. it will get the byte requested
//    if the Pinball 6800 is writing to the address in conflict the write will not be ignored
//    currently there is no mechanism to avoid this failed write to RAM as the BusyR has no connection to the Pinball 6800
//    If the write by the Pinball 6800 is a push of return address to the stack register then a crash is eminent 
//
//    To avoid a crash do not read or write to sensitive areas in the RAM. Stack area is a huge risk.
//    There is a potential to miss a score update if a write to the monitored score location is blocked
//
//    If the Pinball 6800 is busy using an address the BusyL signal is pulled low to case the Atmel to wait on the RAM read or write.
//    as of 2023-01-21 this code below will let the Atmel complete the IO with only a small delay.   
//    BUSY_ is IDC7132 pin46 that is monitored by the Atmel PIN_PD7 physical pin 21
// 
// 2023-01-21 working on while delay when busy signal is low during Atmel reads or writes
// 2023-01-18 Toighten up the memory read and write looks use define for macro to write port bits
// 2023-01-11 save and load testing to Dual Port RAM
// 2022-12-24 Look at parser to accept Hex input, Tim Gopaul
// 2022-10-18 Test program for Dual port RAM 7132
// Tim Gopaul
//
// Changelog
// 2022-11-05 Saturday - Check to see why preview text is not availalble from Uart 1, user \r\n at end of line
// 2022-10-18 Move serial IO all to main serial channel.
//    Add define for debug mode build
//
// PORTA output Low byte of Address 
// PORTC output High byte of Address althouth only bits 0,1,2 are used on for 11 bit address.
// PORTD bits 4, 5, and 6 are used as outputs, bit 7 is used as input for RAM busy wait signal
// PORTB alternates between input and output for use by data read and write.
// Serial is connected to USB and used for programming and diagnstics
// Serial1 is the communication channel to the Atmel284 to a ESP 32 for wifi
// Serial uses PIN_PD0 uart0 recieve and PIN_PD1 uart0 send
// Serial1 uses PIN_PD2 uart1 recieve and and PIN_PD3 uart1 send
// ...first implementation will use Serial for both programming and commands with Serial1 diagnostics

// Intel HEX read and write functions derived from Paul Stoffregen code
// https://www.pjrc.com/tech/8051/ihex.c

/* Intel HEX read/write functions, Paul Stoffregen, paul@ece.orst.edu */
/* This code is in the public domain.  Please retain my name and */
/* email address in distributed copies, and let me know about any bugs */

//#define _DEBUG_
#define MAXHEXLINE 16     //for Hex record length
const int ramSize =  2048;  

#define CommandMode 1     //inputMode will flip between command and data entry
#define DataMode 2        // 2023-01-09 Tim Gopaul
int inputMode = 1;

#include "CommandLine.h"


#define DDRB_Output DDRB = B11111111   // all 1's is output for Atmega1284 PortB to write to IDC-7132 RAM
#define DDRB_Input DDRB = B00000000    // set Atmega1284 Port B back to high inpeadence input all 0's 

#define CEL_LOW  PORTD &=B11101111  // ChipEnable Left LOW PORTD PIN_PD4
#define CEL_HIGH PORTD |=B00010000  // ChipEnable Left HIGH PORTD PIN_PD4

#define RWL_LOW  PORTD &=B11011111  // R/W Left LOW PORTD PIN_PD5
#define RWL_HIGH PORTD |=B00100000  // R/W Left HIGH PORTD PIN_PD5

#define OEL_LOW  PORTD &=B10111111  // OEL LEFT LOW PORTD PIN_PD6 
#define OEL_HIGH PORTD |=B01000000  // OEL LEFT HIGH PORTD PIN_PD6 

#define CEL_OEL_LOW   PORTD &=B10101111  // ChipEnable with OutputEnable LOW PORTD PIN_PD6 PIN_PD4
#define CEL_OEL_HIGH  PORTD |=B01010000  // ChipEnable with OutputEnable HIGH PORTDPIN_PD6  PIN_PD4

#define CEL2_LOW  PORTD &=B11110111  // ChipEnable Left LOW PORTD PIN_PD3
#define CEL2_HIGH PORTD |=B00001000  // ChipEnable Left HIGH PORTD PIN_PD3

#define CEL2_OEL_LOW   PORTD &=B10110111  // ChipEnable with OutputEnable LOW PORTD PIN_PD6 PIN_PD3
#define CEL2_OEL_HIGH  PORTD |=B01001000  // ChipEnable with OutputEnable HIGH PORTDPIN_PD6  PIN_PD3


const byte BUSY_ = PIN_PD7;        // BUSY#  input pull up


volatile byte ramBuffer[ramSize];   // This is an array to hold the contents of memory
                           // Is there enough RAM to hold this on an ATMEGA1284? yes16KBytes
                           // The ATmega1284 provides the following features: 
                           // 128Kbytes of In-System Programmable Flash with 
                           // Read-While-Write capabilities, 4Kbytes EEPROM, 16Kbytes SRAM,
                           // 32 general purpose I/O lines, 32 general purpose working registers, 
                           // Real Time Counter (RTC), three flexible Timer/Counters with compare 
                           // modes and PWM, two serial ...

// ****** ramBufferInit *****
void ramBufferInit(){
  for (int address = 0; address < ramSize; address++) {
    ramBuffer[address] = 0;
    }
}

// ****** helpText *****
int helpText(){
  Serial.println(">**********************************");
  Serial.println(">*                                *");
  Serial.printf(">*   Compile Date: %s\n", __DATE__ );
  Serial.printf(">*   Compile Time: %s\n", __TIME__ );
  Serial.println(">*                                *");
  Serial.println(">*   Untility program to read RAM *");
  Serial.println(">*   Tim Gopaul for Protospace    *");
  Serial.println(">*                                *");
  Serial.println(">*   Enter numbers as baseTen,or  *");
  Serial.println(">*   Enter as 0xNN for hex format *");
  Serial.println(">*                                *");
  Serial.println(">*   add   integer integer        *");
  Serial.println(">*   sub   integer integer        *");
  Serial.println(">*   read  address                *");
  Serial.println(">*   write address databyte       *");
  Serial.println(">*   dump  start count            *");
  Serial.println(">*   dumpBuffer  start count      *");
  Serial.println(">*   fill  start count databyte   *");
  Serial.println(">*   fillRandom  start count      *");
  Serial.println(">*   save startAddress count      *");
  Serial.println(">*   load Intelhex record line    *");
  Serial.println(">*   testMemory start count       *");
  Serial.println(">*                                *");
  Serial.println(">*   game commands work directly  *");
  Serial.println(">*   Game RAM.                    *");
  Serial.println(">*   Use only when pinball off    *");
  Serial.println(">*                                *"); 
  Serial.println(">*   gameRead address             *");
  Serial.println(">*   gameWrite address databyte   *");
  Serial.println(">*   gameDump start count         *");
  Serial.println(">*                                *"); 
  
  Serial.println(">*                                *");
  Serial.println(">*   Enter numbers as decimal or  *");
  Serial.println(">*   0xNN  0X55 for HEX           *");
  Serial.println(">*                                *");
  Serial.println(">**********************************");
  Serial.println();  
  return(0);
}

unsigned int smaller( unsigned int a, unsigned int b){  
  return (b < a) ? b : a;
}


// ****** writeAddress *****
void writeAddress(unsigned int address, byte dataByte){
  PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
  PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

  #ifdef _DEBUG_
    Serial.printf("Writing Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
  #endif

  DDRB_Output;        // all 1's is output for Atmega1284 PortB to write to IDC-7132 RAM
  PORTB = dataByte;   // put the data bits on the data output
  
  RWL_LOW;  //set RW Left to low for writing to RAM digitalWrite(RWL_, LOW)
  CEL_LOW;  //enable the memory chip digitalWrite(CEL_, LOW) 
            //Busy signal is activated low only when the other side is in the same RAM location and CE has gone low
  //write memory cycle is 6580ns 6.58us with this wait check
  while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
    Serial.printf("> RAM BUSY_\r\n");
  } // Wait if the dual port Memory is busy

  CEL_HIGH; //digitalWrite(CEL_, HIGH)  // CEL_ goes high before RWL_ this lets Data stay valid on rising edge of CEL_
  RWL_HIGH; //digitalWrite(RWL_, HIGH)
  
  DDRB_Input;         // set Atmega1284 Port B back to high inpeadence input all 0's
}

// ****** readAddress *****
byte readAddress(unsigned int address){

  PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
  PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

//  OEL_LOW;  //Set Output enable Left to low for outputing from RAM
//  CEL_LOW;  //Chip Enable Left to low for reading from RAM
  CEL_OEL_LOW; //Try a combined bit definition in a single instruction
  __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds
  __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds

//  332ns with one delay
//  264ns without the delay

//  while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
//    Serial.printf("> RAM BUSY_\r\n");
//  } // Wait if the dual port Memory is busy
  
  byte dataByte = PINB;

//  CEL_HIGH;       // deselect RAM chip
//  OEL_HIGH;       // disable the output
  CEL_OEL_HIGH; //Try a combined bit definition in a single instruction

  #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
  #endif

  return dataByte; 
}

// This is a bit of a hack
// I added the second upper RAM after writing the main memory read and right routines.
// Rather than recoding the routines to I have cut and pasted the working code with a small edit to work 
// with the CE pin of the upper ROM
//
// if it works.. It would be good to recode the main program to incorperate the funciton rather the the 99$% duplicate code
// 2023-02-01 Tim Gopaul
//
// PIN_PD3 will allow save and load of game rom on second dual port ram when the pinball 6800 is powered down

// This is a second buffer that holds a copy of the live Game RAM
// This RAM should only be read or or written to when the main Pinball process is powered down.
// May add some provision that holding the Pinball Reset low could be used to alter the gameRAM
// The main reson for these routines are to check to make sure the shadow copy of the RAM is not far different
// than the live GAME rame
// Recall the Shadow RAM is a write only configuration from the PIN ball processors point of view.
// Writes to game RAM by the pinball process are writes to the shadow RAM
// A read from the game RAM passes a copy of the byte to refresh the Shadow RAM with a Write
// The reason.. if the Atmega 1284 is reading the Shadow RAM address the Shadow RAM by receive a Busy and not commit
// the write from the Pinball machine in to the shadow copy.
// ..On the Atmel some routines may be needed to validate that the Shadow RAM copy is close enough.
// For game scores the effect might be that occasionally the Atmega is a step behind in the score.. but at game
// end any update will catch up.

volatile byte gameRamBuffer[ramSize];   // This is an array to hold the contents of memory
                           // Is there enough RAM to hold this on an ATMEGA1284? yes16KBytes
                           // The ATmega1284 provides the following features: 
                           // 128Kbytes of In-System Programmable Flash with 
                           // Read-While-Write capabilities, 4Kbytes EEPROM, 16Kbytes SRAM,
                           // 32 general purpose I/O lines, 32 general purpose working registers, 
                           // Real Time Counter (RTC), three flexible Timer/Counters with compare 
                           // modes and PWM, two serial ...

// ****** gameWriteAddress *****
void gameWriteAddress(unsigned int address, byte dataByte){
  PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
  PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

  #ifdef _DEBUG_
    Serial.printf("Writing Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
  #endif

  DDRB_Output;        // all 1's is output for Atmega1284 PortB to write to IDC-7132 RAM
  PORTB = dataByte;   // put the data bits on the data output
  
  RWL_LOW;  //set RW Left to low for writing to RAM digitalWrite(RWL_, LOW)
  CEL2_LOW;  //enable the memory chip digitalWrite(CEL2_, LOW) 
            //Busy signal is activated low only when the other side is in the same RAM location and CE has gone low
  //write memory cycle is 6580ns 6.58us with this wait check
  while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
    Serial.printf("> RAM BUSY_\r\n");
  } // Wait if the dual port Memory is busy

  CEL2_HIGH; //digitalWrite(CEL2_, HIGH)  // CEL2_ goes high before RWL_ this lets Data stay valid on rising edge of CEL2_
  RWL_HIGH; //digitalWrite(RWL_, HIGH)
  
  DDRB_Input;         // set Atmega1284 Port B back to high inpeadence input all 0's
}

// ****** gameReadAddress *****
byte gameReadAddress(unsigned int address){

  PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
  PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

//  OEL_LOW;  //Set Output enable Left to low for outputing from RAM
//  CEL2_LOW;  //Chip Enable Left to low for reading from RAM
  CEL2_OEL_LOW; //Try a combined bit definition in a single instruction
  __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds
  __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds

//  332ns with one delay
//  264ns without the delay

//  while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
//    Serial.printf("> RAM BUSY_\r\n");
//  } // Wait if the dual port Memory is busy
  
  byte dataByte = PINB;

//  CEL2_HIGH;       // deselect RAM chip
//  OEL_HIGH;       // disable the output
  CEL2_OEL_HIGH; //Try a combined bit definition in a single instruction

  #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
  #endif

  return dataByte; 
}

// ***** gameDumpRange *****
void gameDumpRange(unsigned int addrStart, unsigned int addrCount){
// 2023-01-26 call to fill the buffer with the range added. Tim G

  gameRefreshBuffer(addrStart, addrCount); // The buffer has read the memory now dump to the screen

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on gameRamBuffer index 

  if ((addrStart % 16) != 0) Serial.printf("0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd; address++) { 

    if ((address % 16) == 0) Serial.printf("0x%04X: ", address);
    Serial.printf("0x%02X ",gameRamBuffer[address]);
    if (((address % 16) == 15) | (address == (addrEnd -1)))  Serial.println();
  }
}

// ***** refreshBuffer *****
void gameRefreshBuffer(unsigned int addrStart, unsigned int addrCount){
// this will fill the buffer first

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on gameRamBuffer index 

  OEL_LOW;        //OEL_ low in preparation for CEL2_


  for (unsigned int address = addrStart; address < addrEnd; address++) { 
    PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
    PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

    CEL2_LOW;                            // two NOP in Assembly code give a memory read time of 312 ns
    __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds
    __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds    

//  2023-01-26 checking busy signal also gives time for address and dta to settle befoe reading locked in on CEL going high edge
//    while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
//      Serial.printf("> RAM BUSY_\r\n");
//      } // Wait if the dual port Memory is busy
    
    byte dataByte = PINB;
    CEL2_HIGH;             // deselect RAM chip as soon as read is done
    gameRamBuffer[address] = dataByte;  // load it into the buffer array to do printing later

  }
  
  OEL_HIGH;       // disable the output
}                 // void refreshBuffer(unsigned int addrStart, unsigned int addrCount){


// ***** dumpBuffRange *****
void gameDumpBuffRange(unsigned int addrStart, unsigned int addrCount){

  gameRefreshBuffer(addrStart, addrCount); // The buffer has read the memory now dump to the screen

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on gameRamBuffer index 

  Serial.printf("> Dump Buffer: 0x%04X: To Address Data: 0x%04X: \n", addrStart, addrEnd -1);
  if ((addrStart % 16) != 0) Serial.printf("\n0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd; address++) { 
  
    if ((address % 16) == 0) Serial.printf("\n0x%04X: ", address);
    Serial.printf("0x%02X ", gameRamBuffer[address]);

    #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\n", address, gameRamBuffer[address]);
    #endif
  }
  Serial.println();
  Serial.println();

//Dump the buffer displaying contents as ASCII if printable

  Serial.printf("> Dump Buffer ASCII: 0x%04X: To Address Data: 0x%04X: \n", addrStart, addrEnd -1);
  Serial.println();
  
  //creat column headings from low address nibble
  Serial.print("        "); //print some leading space
  for (unsigned int i = 0; i <= 0x0f;i++)
    Serial.printf( "%1X ",i);
  
  if ((addrStart % 16) != 0) Serial.printf("\n0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd ; address++) { 
  
    if ((address % 16) == 0) Serial.printf("\n0x%04X: ", address);
    if (isPrintable(gameRamBuffer[address]))
      Serial.printf("%c ", (char)gameRamBuffer[address]);
    else
      Serial.printf("%c ", ' ');
  }
  
  Serial.println();
  Serial.println(); 

  //call the saveMemory function to see if it displays the buffer properly
  gameSaveMemory(addrStart, addrCount);

} //void gameDumpBuffRange(unsigned int addrStart, unsigned int addrCount)



// ***** gameSaveMemory *****
void gameSaveMemory(unsigned int addrStart, unsigned int addrCount){

  gameRefreshBuffer(addrStart, addrCount); //This will copy the physical IDC7132 RAM to the Atmel gameRamBuffer[2048]
                                       
  // Only refresh the buffer with the range of bytes needed to avoid contention.
  // copy the RAM memory to a buffer array before processing output
  // Global array is used gameRamBuffer[2048] 

  int bytesToSave = addrCount;                //initialize to the number of bytes to save and decrement for each record / line
  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);
  int recordType = 0x00;                      //Record Type
                                              // tt is the field that represents the HEX record type, which may be one of the following:
                                              // 00 - data record
                                              // 01 - end-of-file record
  int address = addrStart;
  int bytesThisLine;
  
  Serial.printf("\n> Save Memory: 0x%04X: To Address: 0x%04X: \n", addrStart, addrEnd -1);
  
  while (bytesToSave > 0) {
 
    if (bytesToSave > MAXHEXLINE)
      bytesThisLine = MAXHEXLINE;
    else 
      bytesThisLine = bytesToSave;

    int chksum = bytesThisLine + highByte(address) + lowByte(address) + recordType;
    chksum &= 0xFF;
    int linePos = 0;  // initiallize line position left and count the hex output to MAXHEXLINE
    Serial.printf(":%02X%04X%02X", bytesThisLine, address, recordType); 
    while (linePos < bytesThisLine) {
      Serial.printf("%02X", gameRamBuffer[address]);
      chksum += gameRamBuffer[address] & 0xFF; 
      linePos+=1;
      address+=1;
    }
    Serial.printf("%02X\n", (~chksum+1)& 0xFF);
    bytesToSave -=bytesThisLine;
   }

  recordType = 0x01;            //   no address no databytes 01 - end-of-file record
  Serial.printf(":00000001FF\n");  /* end of file marker */

}

// ***** fillRange *****
void fillRange(unsigned int addrStart, unsigned int addrCount, byte dataByte){
  //configure to write to RAM
  DDRB_Output;        // all 1's is output for Atmega1284 PortB to write to IDC-7132 RAM
  RWL_LOW;            //this is a bulk write so keep RWL_ low using CEL_ to trigger write

  PORTB = dataByte;                                         // filling the range with the same byte. set it out side of loop once
  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);                            //bounds check on ramBuffer index  
  for (unsigned int address = addrStart; address < addrEnd; address++) { 
    PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
    PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

    #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
    #endif

    CEL_LOW;

    while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
      Serial.printf("> RAM BUSY_\r\n");
    } // Wait if the dual port Memory is busy

    CEL_HIGH;
        
  } //loop back for next write
  RWL_HIGH;             //this is a bulk write so keep RWL_ low using CEL_ to trigger write return to data input direction           
  DDRB_Input;           // set Atmega1284 Port B back to high inpeadence input all 0's 
  Serial.printf("> fillRange addrStart 0x%04X, addrCount 0x%04X, data 0x%02X\n", addrStart, addrCount, dataByte);
}

// ****** fillRandomRange *****
// this function receives a random databyte but needs to make its own for the fill
void fillRandomRange(unsigned int addrStart, unsigned int addrCount, byte dataByte){

unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on ramBuffer index 
  //configure to write to RAM
  DDRB_Output;    // all 1's is output for Atmega1284 PortB to write to IDC-7132 RAM

//RWL_LOW;        //this is a bulk write so keep RWL_ low using CEL_ to trigger write
  
  for (unsigned int address = addrStart; address < addrEnd; address++) { 

    #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\r\n", address, dataByte);
    #endif    
    
    PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
    PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address
    
    while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
      Serial.printf("> RAM BUSY_\r\n");
    } // Wait if the dual port Memory is busy

    dataByte = (byte)random(0x100);
    PORTB = dataByte;

    RWL_LOW;        //try write low per byte rather than bulk
    CEL_LOW;
    CEL_HIGH;
    RWL_HIGH;

    } //go back for next address write
    
  RWL_HIGH;
  DDRB_Input;        // set Atmega1284 Port B back to high inpeadence input all 0's 

}

// ***** dumpRange *****
void dumpRange(unsigned int addrStart, unsigned int addrCount){
// 2023-01-26 call to fill the buffer with the range added. Tim G

  refreshBuffer(addrStart, addrCount); // The buffer has read the memory now dump to the screen

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on ramBuffer index 

  if ((addrStart % 16) != 0) Serial.printf("0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd; address++) { 

    if ((address % 16) == 0) Serial.printf("0x%04X: ", address);
    Serial.printf("0x%02X ",ramBuffer[address]);
    if (((address % 16) == 15) | (address == (addrEnd -1)))  Serial.println();
  }
}

// ***** refreshBuffer *****
void refreshBuffer(unsigned int addrStart, unsigned int addrCount){
// this will fill the buffer first

#ifdef _DEBUG_
  Serial.printf("> debug just called to refresh ramBuffer 0x%04X\n", (int)ramBuffer);
#endif

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on ramBuffer index 

  OEL_LOW;        //OEL_ low in preparation for CEL_


  for (unsigned int address = addrStart; address < addrEnd; address++) { 
    PORTC = highByte(address);      //Set port C to the high byte of requested RAM address
    PORTA = lowByte(address);       //Set Port A to the low byte of the requested RAM address

    CEL_LOW;                            // two NOP in Assembly code give a memory read time of 312 ns
    __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds
    __asm__ __volatile__ ("nop\n\t");   // take a nap.. a short nap 62.5 nanoseconds    

//  2023-01-26 checking busy signal also gives time for address and dta to settle befoe reading locked in on CEL going high edge
//    while (digitalRead(BUSY_) == LOW){ // 15 is PIN_PD7 in arduino assignment 
//      Serial.printf("> RAM BUSY_\r\n");
//      } // Wait if the dual port Memory is busy
    
    byte dataByte = PINB;
    CEL_HIGH;             // deselect RAM chip as soon as read is done
    ramBuffer[address] = dataByte;  // load it into the buffer array to do printing later

  }
  
  OEL_HIGH;       // disable the output
}                 // void refreshBuffer(unsigned int addrStart, unsigned int addrCount){


// ***** dumpBuffRange *****
void dumpBuffRange(unsigned int addrStart, unsigned int addrCount){

  refreshBuffer(addrStart, addrCount); // The buffer has read the memory now dump to the screen

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);     //bounds check on ramBuffer index 

  Serial.printf("> Dump Buffer: 0x%04X: To Address Data: 0x%04X: \n", addrStart, addrEnd -1);
  if ((addrStart % 16) != 0) Serial.printf("\n0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd; address++) { 
  
    if ((address % 16) == 0) Serial.printf("\n0x%04X: ", address);
    Serial.printf("0x%02X ", ramBuffer[address]);

    #ifdef _DEBUG_
    Serial.printf("Reading Address: 0x%04X: Data: 0x%02X\n", address, ramBuffer[address]);
    #endif
  }
  Serial.println();
  Serial.println();

//Dump the buffer displaying contents as ASCII if printable

  Serial.printf("> Dump Buffer ASCII: 0x%04X: To Address Data: 0x%04X: \n", addrStart, addrEnd -1);
  Serial.println();
  
  //creat column headings from low address nibble
  Serial.print("        "); //print some leading space
  for (unsigned int i = 0; i <= 0x0f;i++)
    Serial.printf( "%1X ",i);
  
  if ((addrStart % 16) != 0) Serial.printf("\n0x%04X: ", addrStart);
  for (unsigned int address = addrStart; address < addrEnd ; address++) { 
  
    if ((address % 16) == 0) Serial.printf("\n0x%04X: ", address);
    if (isPrintable(ramBuffer[address]))
      Serial.printf("%c ", (char)ramBuffer[address]);
    else
      Serial.printf("%c ", ' ');
  }
  
  Serial.println();
  Serial.println(); 

  //call the saveMemory function to see if it displays the buffer properly
  saveMemory(addrStart, addrCount);

} //void dumpBuffRange(unsigned int addrStart, unsigned int addrCount)


// ***** saveMemory *****

/* Intel HEX read/write functions, Paul Stoffregen, paul@ece.orst.edu */
/* This code is in the public domain.  Please retain my name and */
/* email address in distributed copies, and let me know about any bugs */
// https://www.pjrc.com/tech/8051/ihex.c

/* Given the starting address and the ending address */
/* write out Intel Hex format file */

/*
 * Record Format
  An Intel HEX file is composed of any number of HEX records. 
  Each record is made up of five fields that are arranged in the following format:
  
  :llaaaatt[dd...]cc
  
  Each group of letters corresponds to a different field, and each letter represents a single hexadecimal digit.
  Each field is composed of at least two hexadecimal digits-which make up a byte-as described below:

  : is the colon that starts every Intel HEX record.

  ll is the record-length field that represents the number of data bytes (dd) in the record.

  aaaa is the address field that represents the starting address for subsequent data in the record.

  tt is the field that represents the HEX record type, which may be one of the following:
  00 - data record
  01 - end-of-file record
  02 - extended segment address record
  04 - extended linear address record
  05 - start linear address record (MDK-ARM only)

  dd is a data field that represents one byte of data. A record may have multiple data bytes. 
  The number of data bytes in the record must match the number specified by the ll field.
  
  cc is the checksum field that represents the checksum of the record. 
  The checksum is calculated by summing the values of all hexadecimal digit pairs in the record modulo 256
  and taking the two's complement.
 */

// ***** saveMemory *****
void saveMemory(unsigned int addrStart, unsigned int addrCount){

  refreshBuffer(addrStart, addrCount); //This will copy the physical IDC7132 RAM to the Atmel ramBuffer[2048]
                                       
  // Only refresh the buffer with the range of bytes needed to avoid contention.
  // copy the RAM memory to a buffer array before processing output
  // Global array is used ramBuffer[2048] 

  int bytesToSave = addrCount;                //initialize to the number of bytes to save and decrement for each record / line
  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);
  int recordType = 0x00;                      //Record Type
                                              // tt is the field that represents the HEX record type, which may be one of the following:
                                              // 00 - data record
                                              // 01 - end-of-file record
  int address = addrStart;
  int bytesThisLine;
  
  Serial.printf("\n> Save Memory: 0x%04X: To Address: 0x%04X: \n", addrStart, addrEnd -1);
  
  while (bytesToSave > 0) {
 
    if (bytesToSave > MAXHEXLINE)
      bytesThisLine = MAXHEXLINE;
    else 
      bytesThisLine = bytesToSave;

    int chksum = bytesThisLine + highByte(address) + lowByte(address) + recordType;
    chksum &= 0xFF;
    int linePos = 0;  // initiallize line position left and count the hex output to MAXHEXLINE
    Serial.printf(":%02X%04X%02X", bytesThisLine, address, recordType); 
    while (linePos < bytesThisLine) {
      Serial.printf("%02X", ramBuffer[address]);
      chksum += ramBuffer[address] & 0xFF; 
      linePos+=1;
      address+=1;
    }
    Serial.printf("%02X\n", (~chksum+1)& 0xFF);
    bytesToSave -=bytesThisLine;
   }

  recordType = 0x01;            //   no address no databytes 01 - end-of-file record
  Serial.printf(":00000001FF\n");  /* end of file marker */

}

// ***** loadMemory *****
void loadMemory(){
  Serial.printf("> Waiting for Intel Hex input records or end of file record :00000001FF\n");
  inputMode = DataMode;  
  // This flips to DataMode so that main loop will dispatch input to build Intel Hex input line
  // once in DataMode the main loop will add characters to a buffer line until enter is pressed Linefeed.
  // in DataMode each line is interpretted as an Intel Hex record.. type 01 and type 00 supported
  // to leave DataMode the input must receive the Intel Hex end of file record.
  // :00000001FF 
  // .. add CTRL-C and esc as ways to terminate the input
}

// ***** compareBuffer *****
void compareBuffer( unsigned int addrStart, unsigned int addrCount){

  unsigned int addrEnd = smaller((addrStart + addrCount), ramSize);                            //bounds check on ramBuffer index  
  for (unsigned int address = addrStart; address < addrEnd; address++) { 
    byte ramByte = readAddress(address);
    byte buffByte = ramBuffer[address]; 
    if (ramByte != buffByte){ 
      Serial.printf("address 0x%04X: ramBuffer 0x%02X buffByte 0x%02X\n", address, ramByte, buffByte );
      Serial.println("  Subtest");
      for (int i=0; i<10; i++) {
        byte ramByte = readAddress(address);
        byte buffByte = ramBuffer[address]; 
        Serial.printf(" address 0x%04X: ramBuffer 0x%02X buffByte 0x%02X\n", address, ramByte, buffByte );
      }
    }
  }        
}
//compareBuffer

// ***** testMemory *****
void testMemory(unsigned int addrStart, unsigned int addrCount, int testLoops) {

  for (int i = 0; i < testLoops; i++){
    Serial.printf(">Memory loop test %d\n", i);  
    fillRandomRange(addrStart, addrCount, 1); //dataByte is recreated for each address of range
    refreshBuffer(addrStart, addrCount);
    compareBuffer(addrStart, addrCount);
  }
}



// ***** setup ***** -----------------------------------------------
void setup() {

  // seed the random mumber generator
  randomSeed(millis()); //initialize pseudo random number
  
  delay(200);
  Serial.begin(115200);
  delay(100);
  helpText();

  #ifdef _DEBUG_
    Serial.println("_DEBUG_ is defined");
  #endif
  
  DDRA = B11111111;     // PortA 0 to 7 are the low address out for the 2Kx8 RAM
  PORTA = B00000000;     // Set low Address bits low
  DDRC = B11111111;   // PORTC 0 - 7  are the high byte of address output  only 0,1,2 used
  PORTC = B00000000;     //Set high Address bits low
  
// For DATA bus PortB will be used alternating between input and output
  PORTB = B00000000;       //set pullups but maybe not needed PORTB is output PINB is input
  DDRB = B00000000;        // all 0's is input for PortB

  //configure control lines as output except busy line is input
  //CEL_ = PIN_PD4;         // CEL#   output high
  //RWL_ = PIN_PD5;         // R/WL#  output high
  //OEL_ = PIN_PD6;         // OEL#   output high
  //BUSY_ = PIN_PD7;        // BUSY#  input pull up

  DDRD |= B01111100;        // set DDR pins 4 5 6 to output..do this at setup
  PORTD |=B01111100;        // bits 3 and two are also outputs here. Bits 0 and 1 are reserved for Serial

  CEL_HIGH;   //PIN_PD4 CEL#   output high
  RWL_HIGH;   //PIN_PD5 R/WL#  output high
  OEL_HIGH;   //PIN_PD6 OEL#   output high

  pinMode(BUSY_,INPUT_PULLUP); // 15 is PIN_PD7 in arduino assignment this is the busy signal from dual port ram

} //endof Setup

// ***** loop ***** ----------------------------------------
void loop() {
  bool received = getCommandLineFromSerialPort(CommandLine);      //global CommandLine is defined in CommandLine.h
  if (received) {
    switch(inputMode){
      case CommandMode:
        DoMyCommand(CommandLine);
        break;
      case DataMode:
        DoMyHexLine(CommandLine);
        break;
      default:
        break;
    }
  }
}


  
