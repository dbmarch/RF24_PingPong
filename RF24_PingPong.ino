#include "printf.h"
#include <nRF24L01.h>
#include <RF24.h>
#include <RF24_config.h>
  
/*
  // March 2014 - TMRh20 - Updated along with High Speed RF24 Library fork
  // Parts derived from examples by J. Coliz <maniacbug@ymail.com>
*/
/**
 * Example for efficient call-response using ack-payloads 
 *
 * This example continues to make use of all the normal functionality of the radios including
 * the auto-ack and auto-retry features, but allows ack-payloads to be written optionally as well.
 * This allows very fast call-response communication, with the responding radio never having to 
 * switch out of Primary Receiver mode to send back a payload, but having the option to if wanting
 * to initiate communication instead of respond to a commmunication.
 */
 


#include <SPI.h>
#include "nRF24L01.h"
#include "RF24.h"
#include "printf.h"


#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>

//#include "scheduler.h"



typedef enum { role_ping_out = 1, role_pong_back } role_e ;                 // The various roles supported by this sketch
const char* role_friendly_name[] = { "invalid", "Ping out", "Pong back"};  // The debug-friendly names of those roles


#define DETECT_PIN    7


#define I2C_ADDR_1      0x27        // I2C address of PCF8574A
#define I2C_ADDR_2      0x3F      // I2C address of PCF8574A


role_e role = role_ping_out;                                              // The role of the current running sketch



#define BACKLIGHT_PIN 3
#define En_pin        2
#define Rw_pin        1
#define Rs_pin        0
#define D4_pin        4
#define D5_pin        5
#define D6_pin        6
#define D7_pin        7

LiquidCrystal_I2C lcd1(I2C_ADDR_1, 2 ,1,0,4,5,6,7,3,POSITIVE);
LiquidCrystal_I2C lcd2(I2C_ADDR_2, 2 ,1,0,4,5,6,7,3,POSITIVE);

LiquidCrystal_I2C *lcd = 0;;

// Hardware configuration: Set up nRF24L01 radio on SPI bus plus pins 7 & 8 
//RF24 radio(7,8);
RF24 radio(9,10);

// Topology
const uint64_t pipes[2] = { 0xABCDABCD71LL, 0x544d52687CLL };              // Radio pipe addresses for the 2 nodes to communicate.

// Role management: Set up role.  This sketch uses the same software for all the nodes
// in this system.  Doing so greatly simplifies testing.  


// A single byte to keep track of the data being sent back and forth
unsigned long counter = 1;
unsigned long rxCount = 0;
int errCnt = 0;
unsigned lastPing = 0;
unsigned pingMin=1000;
unsigned pingMax=0;

void updateDisplay(void);
void resetStats(void);
int myLCDaddr=0;

void setupLCD (void)
{
  pinMode (DETECT_PIN, INPUT);
  if ( digitalRead(DETECT_PIN) == LOW)
  {
     lcd = &lcd1;
     myLCDaddr=I2C_ADDR_1;
     role = role_pong_back;
     
  }
  else 
  {
     lcd = &lcd2;
     myLCDaddr=I2C_ADDR_2;
     role = role_ping_out;
}
  lcd->begin(20,4);
  
  lcd->clear();
  lcd->home();
  lcd->setBacklight(HIGH);

}


void setup(){
  setupLCD();
      
  Serial.begin(115200);
  printf_begin();
  Serial.print(F("\n\rRF24/examples/pingpair_ack/\n\rROLE: "));
  Serial.println(role_friendly_name[role]);
  Serial.println(F("*** PRESS 'T' to begin transmitting to the other node"));

  Serial.print(F("\nI2C_ADDR: "));
  Serial.println (myLCDaddr);
    // Setup and configure rf radio
  
  radio.begin();
  radio.setAutoAck(1);                    // Ensure autoACK is enabled
  radio.enableAckPayload();               // Allow optional ack payloads
  radio.setRetries(0,15);                 // Smallest time between retries, max no. of retries
  radio.setPayloadSize(1);                // Here we are sending 1-byte payloads to test the call-response speed


   radio.openWritingPipe(pipes[1]);        // Both radios listen on the same pipes by default, and switch when writing
   radio.openReadingPipe(1,pipes[0]);
   radio.startListening();                 // Start listening
 

  if ( role == role_ping_out)
  {
       Serial.println ("Ping_Out");
       radio.openWritingPipe(pipes[0]);
       radio.openReadingPipe(1,pipes[1]);
  }
  else
      Serial.println ("pong_back");
  radio.printDetails();                   // Dump the configuration of the rf unit for debugging


  resetStats();
 
}

void loop(void) {

  if (role == role_ping_out){
    
    radio.stopListening();                                  // First, stop listening so we can talk.
        
    printf("Now sending %u as payload. ",counter);
    byte gotByte;  
    unsigned long time = micros();                          // Take the time, and send it.  This will block until complete   
                                                            //Called when STANDBY-I mode is engaged (User is finished sending)
    if (!radio.write( &counter, 1 )){
      Serial.println(("failed.")); 
      errCnt++;     
    }else{

      if(!radio.available()){ 
        Serial.println(F("Blank Payload Received.")); 
        errCnt++;
      }else{
        while(radio.available() ){
          unsigned long tim = micros();
          radio.read( &gotByte, 1 );
          lastPing=tim-time;
          printf("Got response %d, round-trip delay: %lu microseconds\n\r",gotByte,tim-time);
          
          if (lastPing < pingMin)
            pingMin=lastPing;
          if (lastPing> pingMax)
             pingMax= lastPing;
          counter++;
        }
      }

    }
    // Try again later
    delay(1000);
  }

  // Pong back role.  Receive each packet, dump it out, and send it back

  if ( role == role_pong_back ) {
    byte pipeNo;
    byte gotByte;                                       // Dump the payloads until we've gotten everything
    while( radio.available(&pipeNo)){
      radio.read( &gotByte, 1 );
      radio.writeAckPayload(pipeNo,&gotByte, 1 );  
      counter = gotByte;  
      rxCount++;
      Serial.print (F("Payload Received: "));
      Serial.println (gotByte);
   }
 }
   updateDisplay();

  // Change roles
  if ( Serial.available() )
  {
    char c = toupper(Serial.read());
    if ( c == 'T' && role == role_pong_back )
    {
      Serial.println(F("*** CHANGING TO TRANSMIT ROLE -- PRESS 'R' TO SWITCH BACK"));

      role = role_ping_out;                  // Become the primary transmitter (ping out)
      radio.openWritingPipe(pipes[0]);
      radio.openReadingPipe(1,pipes[1]);
       resetStats();
    }
    else if ( c == 'R' && role == role_ping_out )
    {
        Serial.println(F("*** CHANGING TO RECEIVE ROLE -- PRESS 'T' TO SWITCH BACK"));
      
       role = role_pong_back;                // Become the primary receiver (pong back)
       radio.openWritingPipe(pipes[1]);
       radio.openReadingPipe(1,pipes[0]);
       radio.startListening();
       resetStats();
    }
  }
}



void updateDisplay(void)
  {
// 123456790123456789012345678901234567890
// ROLE: xxxxxxxxxx         
// CNT:  xxxxxxxxxx    ERR=xxxxxxxxxx
// PING: xxxxxxxxxx
// MIN:  xxxxxxxxxx   MAX xxxxxxxxxx
  char line1[50];
  char line2[50];
  char line3[50];
  char line4[50];

  sprintf (line1, "ROLE: %s",(role==role_ping_out ? "Transmitter" : "Receiver"));
  if (role== role_ping_out)
    {
    sprintf (line2, "CNT: %lu   ERR: %d",  counter, errCnt);
    sprintf (line3, "PING: %u uSec", lastPing);
    sprintf (line4, "MIN: %u  MAX:%u", pingMin, pingMax);
    }
  else
    {
    sprintf (line2, "PAYLOAD: %lu",  counter);
    sprintf (line3, "RxCount: %lu", rxCount);
    line4[0]=0;
    }

  memset (&line1[strlen(line1)], ' ', 20-strlen(line1));
  memset (&line2[strlen(line2)], ' ', 20-strlen(line2));
  memset (&line3[strlen(line3)], ' ', 20-strlen(line3));
  memset (&line4[strlen(line4)], ' ', 20-strlen(line4));
 
  line1[20]=0;
  line2[20]=0;
  line3[20]=0;
  line4[20]=0;
  
  
  lcd->setCursor(0, 0);
  lcd->write (line1);
  lcd->setCursor(0, 1);
  lcd->write (line2);
  lcd->setCursor(0, 2);
  lcd->write (line3);
  lcd->setCursor(0, 3);
  lcd->write (line4);
  }


 void resetStats(void)
 {
      errCnt=0;
      lastPing=0;
      pingMin=1000;
      pingMax=0;
      rxCount = 0;
      lcd->clear();
 }

