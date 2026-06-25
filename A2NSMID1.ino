//A2NSMID1: MIDI IN CAPTURE, FILTER, TRANSFORM, OUTPUT

//2026-04-22: send patch change to clarinet at startup
//2026-04-16: switches for channels 11-16 now used for transposition, read at startup time only
// channel 16 = sign bit: 1 = negative, 0=positive
// channels 15 thru 11 are a 5 bit number: 15->16, 11=>1
// the calculated transposevalue is applied to note on and note off events.

//Requires curcuitry for MIDI IN feeding in to Serial1 Rx1
//MIDI OUT is sent from Serial1 Tx1, Thru to Tx2 and TX3

//Uses LCD Keypad Shield - has 5 buttons + reset
#include <LiquidCrystal.h>

//For debugging:
bool circbuf=0; //maintain a circular buffer of midi data received
bool hexdump=0; //dump midi in data to serial monitor (requires circbuf=1)

//Configuration options:
bool filterAftertouch=0;  //filter aftertouch messages
bool filterCtrl=0;        //filter control change messages
bool filterPgm=0;         //filter program change messages
bool filterPressure=0;    //filter channel pressure messages
bool filterBend=0;        //filter pitch bend messages

bool filterChannel[] = {0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0}; //filter channel 10 (drums)
//========================================^ DRUMS

//Enhancement: Set channel filters dynamically using dip switches when no serial data being received.  Smartly debounce the switches.

int delaybetweenbytes = 1000; //slow down for Apple
int transformAftertouch = 0;       //transformation for wind controller - 0 is no transform

//the following 2 values are now being read from dip switches on startup - 
//int transformChannelPressure = 11;  //transformation for wind controller - value is controller number to use (ex: 7=volume 11=expression
//int transformBreathController = 7 ; //convert MIDI control change 2 messages to volume 7

int transformBreathValue = 0;
int transformPressureValue = 0;

bool breathControllerFound = false;
int transposevalue = 0;

byte inbyte;  //current midi in byte
int state = 0;  //state machine

int bufbytes = 1000;  //size of circular buffer
byte midibuf[1000];   //circular buffer
int ixhead=0;         //bytes added at head
int ixtail=0;         //bytes removed at tail

int noteDown = LOW; //note on or note off
int note;

int currchnl; //saves current midi channel from status byte
int data1;    //saves first data byte when midi message has 2 data bytes

 // define some values used by the panel and buttons
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

int lcd_key     = 0;
int adc_key_in  = 0;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5

//LCDKeypad uses A0 for Buttons
int buttonsPin = A0;

void setup() { 
    Serial.begin(9600);
    Serial.println("Please wait 3 seconds"); //prevent problems when uploading sketch

   
    // select the pins used on the LCD panel
    
    lcd.begin(16,2);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Apple ][ No Slot");
    lcd.setCursor(0,1);
    lcd.print("*M I D I*  v0.02");
    
    Serial1.begin(31250);
    Serial2.begin(31250);
    Serial3.begin(31250);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN,HIGH);
    
    delay(3000);
    
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("0123456789ABCDEF");
        
    digitalWrite(LED_BUILTIN,LOW);

    //Read DIP switches
    pinMode(23,INPUT);
    pinMode(25,INPUT);
    pinMode(27,INPUT);
    pinMode(29,INPUT);
    pinMode(31,INPUT);
    pinMode(33,INPUT);
    pinMode(35,INPUT);
    pinMode(37,INPUT);
    pinMode(39,INPUT);
    pinMode(41,INPUT);
    pinMode(43,INPUT);
    pinMode(45,INPUT);
    pinMode(47,INPUT);
    pinMode(49,INPUT);
    pinMode(51,INPUT);
    pinMode(53,INPUT);
    
    filterChannel[0] = digitalRead(23);
    filterChannel[1] = digitalRead(25);
    filterChannel[2] = digitalRead(27);
    filterChannel[3] = digitalRead(29);
    filterChannel[4] = digitalRead(31);    
    filterChannel[5] = digitalRead(33);
    filterChannel[6] = digitalRead(35);
    filterChannel[7] = digitalRead(37);
    filterChannel[8] = digitalRead(39);
    filterChannel[9] = digitalRead(41);    
    filterChannel[10] = digitalRead(43);
    filterChannel[11] = digitalRead(45);
    filterChannel[12] = digitalRead(47);
    filterChannel[13] = digitalRead(49);
    filterChannel[14] = digitalRead(51);    
    filterChannel[15] = digitalRead(53);

    Serial.println("Filtered Channels:");
    lcd.setCursor(0,1);
    for (int i=0; i<=15; i++)
    {
      lcd.print(filterChannel[i]);

      //reset all controllers
      do2ByteMsg(0xB0+i, 121, 0);
      
      Serial.print(filterChannel[i]);
      Serial.print(" ");
    }

    int clarinet = 71;
    do1ByteMsg(0xC0, clarinet);

    transformBreathValue = filterChannel[0]*16 + filterChannel[1]*8 + filterChannel[2]*4 + filterChannel[3]*2 + filterChannel[4];
    transformPressureValue = filterChannel[5]*16 + filterChannel[6]*8 + filterChannel[7]*4 + filterChannel[8]*2 + filterChannel[9];

    transposevalue = filterChannel[10]*16 + filterChannel[11]*8 + filterChannel[12]*4 + filterChannel[13]*2 + filterChannel[14];
    if (filterChannel[15] == 1)
    {
      transposevalue = transposevalue * -1;
    }
    lcd.setCursor(10,0);
    lcd.print(" TR    ");
    lcd.setCursor(13,0);
    lcd.print(transposevalue);
    
    Serial.println("OK to play now");
}

void loop() 
{
  if (Serial1.available() > 0)
  {
    inbyte = Serial1.read();
    if (inbyte > 0xF0)
    {
      state=0xFF;  //ignore all realtime messages
    }
    else if (inbyte == 0xF0)
    {
      state=0xF0;  //ignore sysex messages
    }
    else
    {    
      if (circbuf)
      {
        midibuf[ixhead]=inbyte;
        ixhead++;
        if (ixhead>=bufbytes)
        {
          ixhead = 0;
        }
      }
    }            
    switch(state) {
        case 0:
          doState0();
          break;          
          
        case 0x91:
          if (inbyte < 0x80)
          {
            note = inbyte+transposevalue;
            state = 0x92;
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }
          break;
          
        case 0x92:
          if (inbyte < 0x80)
          {
            doNote(note, inbyte, noteDown);
          }
          state = 0x91;  //may have running status
          break;

        case 0xA1:
          if (inbyte < 0x80)
          {
            data1 = inbyte;
            state = 0xA2;
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }          
          break;
          
        case 0xA2:
          if (inbyte < 0x80)
          {
            if (transformAftertouch > 0)
            {
               do2ByteMsg(0xB0+currchnl, transformAftertouch, inbyte);  
            }
            else
            {
              do2ByteMsg(0xA0+currchnl, data1, inbyte);
            }
          }
          state = 0xA1;  //may have running status          
          break;        
          
        case 0xB1:
          if (inbyte < 0x80)
          {
            data1 = inbyte;
            if (data1 == 2)
            {
              breathControllerFound = true;
            }
            state = 0xB2;
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }                    
          break;
          
        case 0xB2:          
          if (inbyte < 0x80)
          {
            if ((transformBreathValue > 0)  && breathControllerFound)
            {
              data1 = transformBreathValue;
              breathControllerFound = false;
            }
            do2ByteMsg(0xB0+currchnl, data1, inbyte);
          }
          state = 0xB1;  //may have running status          
          break;       
          
        case 0xC1:
          if (inbyte < 0x80)
          {
            do1ByteMsg(0xC0+currchnl, inbyte);
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }                    
          break;     
          
        case 0xD1:          
          if (inbyte < 0x80)
          {
            if (transformPressureValue > 0)
            {
              do2ByteMsg(0xB0+currchnl, transformPressureValue, inbyte);  
            }
            else
            {
              do1ByteMsg(0xD0+currchnl, inbyte);
            }
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }                    
          break;     
          
        case 0xE1:
          if (inbyte < 0x80)
          {
            data1 = inbyte;
            state = 0xE2;
          }
          else  //if status byte received: cancel running status and process status byte 
          {
            state = 0;
            doState0();
          }                    
          break;
                    
        case 0xE2:     
          if (inbyte < 0x80)
          {
            do2ByteMsg(0xE0+currchnl, data1, inbyte);
          }
          state = 0xE1;  //may have running status          
          break;                 
                    
        case 0xF0:
          if (inbyte >= 0x80)  //any status byte (not just F7) ends sysex message
          {
            state = 0;
            doState0();
          }
          break;
        
        case 0xFF:
          if (inbyte >= 0x80)  //any status byte ends realtime message
          {
            state = 0;
            doState0();
          }
          break;
          
    } //switch    
  } //if Serial1 available 
  else  //can do work here while waiting for next message
  {
    if (hexdump)
    {
      while (ixtail != ixhead)
      {
        if (midibuf[ixtail] >= 0x80)
        {
          Serial.println();
        }
        Serial.print(midibuf[ixtail],HEX);
        Serial.print(" ");
        ixtail++;
        if (ixtail >= bufbytes)
        {
          ixtail=0;
        }
      } //while
    } //if hexdump
  } //else
} //loop

void doState0 ()
{
    //stay in state 0 until state changed or a supported status byte received
    if ((inbyte &0xf0) == 0x80)
    {
       currchnl = inbyte & 0x0F;
       noteDown = LOW;
       state = 0x91;
    }
    else if ((inbyte & 0xf0) == 0x90)
    {
       currchnl = inbyte & 0x0F;
       noteDown = HIGH;
       state = 0x91;    
    }
    else if ((inbyte & 0xf0) == 0xA0)
    {
       if (!filterAftertouch)
       {
          currchnl = inbyte & 0x0F;
          state = 0xA1;
       }
    }
    else if ((inbyte & 0xf0) == 0xB0)
    {
       if (!filterCtrl)
       {
          currchnl = inbyte & 0x0F;
          state = 0xB1;
       }
    }
    else if ((inbyte & 0xf0) == 0xC0)
    {
       if (!filterPgm)
       {
          currchnl = inbyte & 0x0F;
          state = 0xC1;
       }
    }    
    else if ((inbyte & 0xf0) == 0xD0)
    {
       if (!filterPressure)
       {
          currchnl = inbyte & 0x0F;
          state = 0xD1;
       }
    }
    else if ((inbyte & 0xf0) == 0xE0)
    {
       if (!filterBend)
       {
          currchnl = inbyte & 0x0F;
          state = 0xE1;
       }
    }
}

void doNote (byte note, byte velocity, int down) {
  if ((down == HIGH) && (velocity == 0))
  {
    down = LOW;
  }
  if (down == LOW)
  {
    //digitalWrite(LED_BUILTIN, LOW);    
    
    Serial1.write(0x90+currchnl); 
    Serial2.write(0x90+currchnl); 
    Serial3.write(0x90+currchnl); 
    doDelay();
    Serial1.write(note);
    Serial2.write(note);
    Serial3.write(note);
    doDelay();
    Serial1.write(0);
    Serial2.write(0);
    Serial3.write(0);
    doDelay();
    
  }
  else
  {    
    if (!(filterChannel[currchnl]))
    {
      //digitalWrite(LED_BUILTIN,HIGH);
      Serial1.write(0x90+currchnl);
      Serial2.write(0x90+currchnl);
      Serial3.write(0x90+currchnl);
      doDelay();
      Serial1.write(note);
      Serial2.write(note);
      Serial3.write(note);
      doDelay();
      Serial1.write(velocity);
      Serial2.write(velocity);
      Serial3.write(velocity);
      doDelay();
    }
  }
}

void do2ByteMsg (byte stsbyte, byte dbyte1, int dbyte2) {    
    if (!(filterChannel[currchnl]))
    {
      Serial1.write(stsbyte);
      Serial2.write(stsbyte);
      Serial3.write(stsbyte);
      doDelay();
      Serial1.write(dbyte1);
      Serial2.write(dbyte1);
      Serial3.write(dbyte1);
      doDelay();
      Serial1.write(dbyte2);
      Serial2.write(dbyte2);
      Serial3.write(dbyte2);
      doDelay();
    }
} 

void do1ByteMsg (byte stsbyte, byte dbyte1) {    
    if (!(filterChannel[currchnl]))
    {  
      Serial1.write(stsbyte);
      Serial2.write(stsbyte);
      Serial3.write(stsbyte);
      doDelay();
      Serial1.write(dbyte1);    
      Serial2.write(dbyte1);    
      Serial3.write(dbyte1);    
      doDelay();
    }
}

void doDelay()
{
    //Following logic will only be done if config file has "DIP=1"
    int changed=0;
    int prev=0;
    for (int ix=0; ix<=15; ix++)
    {
      prev=filterChannel[ix];
      filterChannel[ix] = digitalRead(23+(2*ix));
      changed += (prev != filterChannel[ix]);
    }
    
    //filterChannel[1] = digitalRead(25);
    //filterChannel[2] = digitalRead(27);
    //filterChannel[3] = digitalRead(29);
    //filterChannel[4] = digitalRead(31);    
    //filterChannel[5] = digitalRead(33);
    //filterChannel[6] = digitalRead(35);
    //filterChannel[7] = digitalRead(37);
    //filterChannel[8] = digitalRead(39);
    //filterChannel[9] = digitalRead(41);    
    //filterChannel[10] = digitalRead(43);
    //filterChannel[11] = digitalRead(45);
    //filterChannel[12] = digitalRead(47);
    //filterChannel[13] = digitalRead(49);
    //filterChannel[14] = digitalRead(51);    
    //filterChannel[15] = digitalRead(53);
    
    if (changed)
    {
      lcd.setCursor(0,1);
      for (int i=0; i<=15; i++)
      {
        lcd.print(filterChannel[i]);
      }
    }
    for (int de=0; de < delaybetweenbytes; de++) {}
}

