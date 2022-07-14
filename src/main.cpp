#include <Arduino.h>

/*
 * 0.2 - added offset functionality and moved rendering of timecode to string into the main loop.
 *       also changed optimization in the compiler and linker to -o3
 * 0.3 - 
 */

#define OLED 0
#define FTSEG 1
#define DMIDI 1
#define MVA 0
#define READINGS 0
#define DSER 1

//#if FTSEG
#if FTSEG
#include <Adafruit_I2CDevice.h>
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_GFX.h>

#define DISP1 0x70
#define DISP2 0x71

#endif

#if MVA
#include <movingAvg.h>
#endif

#if OLED
#include <Adafruit_SPIDevice.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// #include <Fonts/FreeSans9pt7b.h>
#endif

#if DMIDI 

#include <USB-MIDI.h>
USBMIDI_CREATE_DEFAULT_INSTANCE();
void sendMTC();

#endif

#include "ltccalc.h"

#define FIRMWARE_VERSION "0.5"

unsigned int mid_time = 600;

uint8_t pin7=1;

#define end_data_position      63
#define end_sync_position      77
#define end_smpte_position     80

volatile unsigned int bit_time=0;
volatile unsigned int bit_time_one=0;
volatile unsigned int bit_time_zero=0;
volatile unsigned int avgBitZero;
volatile unsigned int avgBitOne;

volatile boolean valid_tc_word;
volatile boolean ones_bit_count;
volatile boolean tc_sync;
volatile boolean write_tc_out;
volatile boolean drop_frame_flag;
volatile boolean do_drop_frame_calc;

volatile byte total_bits;
volatile byte current_bit;
volatile byte sync_count;
int kk = 1;

#if MVA
movingAvg bitOne(16);
movingAvg bitZero(16);
#endif
/*
  long TotalFrames_NoOffset = 0; // number representing the total number of frames represented by the incoming timecode. Used for applying an offset.
  // fucked if I know how this works when you got drop_frame to consider.... but I guess this will get worked out
  // eventually.

  long TotalFrames_WithOffset = 0; // number representing the total number of frames _AFTER_ offset has been applied. basically TotalFrames_NoOffset + offset.
  // wrap-around will apply depending on fps.
  int offset = 0; // the offset
*/

byte fps_snd; //fps that we will send via MTC
volatile byte fps = 0; //fps calculated from LTC (not considering non-integer modes)
byte prev_fps=0;
volatile byte max_frames = 0; // temp variable used for calculating fps

volatile struct smpte_frame_struct smpte_frame;
volatile struct smpte_frame_struct smpte_frame_offset;
volatile long LTCFrames = 0;
volatile long LTCFrames_offset = 0;
long offset = 0;

volatile int iInsideVector=0;


volatile byte tc[8];
char timeCode[12];
char timeCode_off[12];
char prev_timeCode[12];
char prev_timeCode_off[12];

char userBit[11];

char szBuf[20];
volatile byte toSend;

volatile struct smpte_frame_struct LTCIn;
volatile struct smpte_frame_struct LTCIn_offset;

byte MTCIndex = 0;

int i = 0;
volatile byte j = 0;
volatile byte k = 0;
volatile byte oldF;

long t5Iterations = 0;

// pin 12 - we are going to toggle6state each time we get a frame come through.
int p12State = 0;
int LTCResetCounter = 0;


#if OLED
Adafruit_SSD1306 display(4);
#endif

//#define LM_SIZE 16

#if FTSEG 
//FTSEG
Adafruit_AlphaNum4 ANDisp1 = Adafruit_AlphaNum4();
Adafruit_AlphaNum4 ANDisp2 = Adafruit_AlphaNum4();
char szDisp[10];
#endif
char szUB[15];


ISR(TIMER3_COMPA_vect) {

  TCNT3 = 0; // NOTE This might benefit from being changed to CTC mode. TODO: Research CTC mode.
  
  //   Subdividing the counter so that we get 4 counts per frame.
  //   per 1 frame we are seeing 64 counts of this interrupt.

#if DMIDI  
  if ((t5Iterations % 8) == 0 && (t5Iterations < 32)) {
     sendMTC();
  }
#endif

  if (LTCIn.f < 2) {
    digitalWrite(9,1);
  } else {
    digitalWrite(9,0);
  }

  t5Iterations++;

}

uint8_t pin8=0;
uint8_t pin8written=0;

uint16_t t0Iterations=0;
uint16_t t0IterationsSaved=0;
ISR(TIMER0_COMPA_vect)
{
  TCNT0=0;
  if (LTCIn.f == 0 && pin8written == 0) {
    // pin8=1-pin8;
    digitalWrite(8,1);
    pin8written=1;
    t0IterationsSaved=t0Iterations;
    t0Iterations=0;
    if (Serial.availableForWrite()) Serial.println(t0IterationsSaved);
  }
  if (LTCIn.f > 1 && pin8written == 1) {
    digitalWrite(8,0);
    pin8written=0;
  }
  t0Iterations++;
}

/* ICR interrupt vector */
/*
   This is where we decode the SMPTE/LTC signal.
*/


ISR(TIMER1_CAPT_vect)
{
  bit_time = ICR1;

  //toggleCaptureEdge
  /*
     Toggle the interrupt edge. If we see a rising edge, the next one we want to see will be falling.
  */
  TCCR1B ^= _BV(6);

  digitalWrite(7,pin7);
  pin7=1-pin7;

  /*
     record the counter time when the edge occurred.
  */


  if (ones_bit_count == true) // only count the second ones pluse
    ones_bit_count = false;
  else
  {
    if (bit_time > mid_time)
    {
      current_bit = 0;
      sync_count = 0;
      bit_time_zero = bit_time;

#if MVA

      avgBitZero=bitZero.reading(bit_time_zero);

#endif

    }
    else //if (bit_time < mid_time)
    {
      ones_bit_count = true;
      current_bit = 1;
      bit_time_one = bit_time;

#if MVA

      avgBitOne=bitOne.reading(bit_time_one);

#endif


      sync_count++;
      if (sync_count == 12) // part of the last two bytes of a timecode word
      {
        sync_count = 0;
        tc_sync = true;
        total_bits = end_sync_position;
      }
    }

    if (total_bits <= end_data_position) // timecode runs least to most so we need
    { // to shift things around
      tc[0] = tc[0] >> 1;

      for (int n = 1; n < 8; n++)
      {
        if (tc[n] & 1)
          tc[n - 1] |= 0x80;

        tc[n] = tc[n] >> 1;
      }

      if (current_bit == 1)
        tc[7] |= 0x80;
    }
    total_bits++;
  }

  if (total_bits == end_smpte_position) // we have the 80th bit
  {
    total_bits = 0;
    if (tc_sync)
    {
      tc_sync = false;
      valid_tc_word = true;
    }
  }

  if (valid_tc_word)
  {

    valid_tc_word = false;

    drop_frame_flag = bit_is_set(tc[1], 2);
    do_drop_frame_calc = drop_frame_flag;

    max_frames = LTCIn.f; // store the frames number for later analysis as to FPS

    LTCIn.h = (tc[6] & 0x0F) + ((tc[7] & 0x03) * 10);
    LTCIn.m = (tc[4] & 0x0F) + ((tc[5] & 0x07) * 10);
    LTCIn.s = (tc[2] & 0x0F) + ((tc[3] & 0x07) * 10);
    LTCIn.f = (tc[0] & 0x0F) + ((tc[1] & 0x03) * 10);

    if (fps>20) {
      LTCFrames = tc2frame(&LTCIn, fps, drop_frame_flag);
      LTCFrames_offset = LTCFrames + offset;
      frame2tc(&LTCIn_offset, LTCFrames_offset, (long)fps, drop_frame_flag);
    }

    // the "+ 0x30" is actually a quick way of assigning an ASCII code to the derived number from
    // the timecode bit buffer.

    LTCIn.ub[10] = 0;
    LTCIn.ub[9] = ((tc[0] & 0xF0) >> 4) + 0x30; // user bits 8
    LTCIn.ub[8] = ((tc[1] & 0xF0) >> 4) + 0x30; // user bits 7
    LTCIn.ub[7] = ((tc[2] & 0xF0) >> 4) + 0x30; // user bits 6
    LTCIn.ub[6] = ((tc[3] & 0xF0) >> 4) + 0x30; // user bits 5
    LTCIn.ub[5] = '-';
    LTCIn.ub[4] = ((tc[4] & 0xF0) >> 4) + 0x30; // user bits 4
    LTCIn.ub[3] = ((tc[5] & 0xF0) >> 4) + 0x30; // user bits 3
    LTCIn.ub[2] = '-';
    LTCIn.ub[1] = ((tc[6] & 0xF0) >> 4) + 0x30; // user bits 2
    LTCIn.ub[0] = ((tc[7] & 0xF0) >> 4) + 0x30; // user bits 1

    write_tc_out = true;

    if (t5Iterations < 32) {
      OCR3A = OCR3A - constrain(pow(2, 32 - t5Iterations), 0, 2048);
    }

    if (t5Iterations > 32) {
      OCR3A = OCR3A + constrain(pow(2, t5Iterations - 32), 0, 2048);
    }

    t5Iterations = 0;
    TCNT3 = OCR3A - 1; //becuase we want to fire the ISR immediately on receipt.

    //if (f == 0 && ((s & 0x01) == 0)) // v0.1
    if (LTCIn.f == 0 && ((LTCIn.s & 0x01) == 0))             // v0.2
    //if (LTCIn.f == 0)             // v0.2
    {
      MTCIndex = 0;
      fps = 1 + max_frames;
      kk=0;
    }

    if (LTCIn.f == 0){
      TCNT0 = OCR0A;
    }

  }

  //reset Timer4
  TCNT1 = 0;
}

#if DMIDI
byte midiMsg[2];

/* send MIDI Timecode Quarter Frame*/
void sendMTC() {

  switch (fps) {
    case 24:
      fps_snd = 0x70;
      break;
    case 25:
      fps_snd = 0x72;
      break;
    case 30:
      if (drop_frame_flag)
        fps_snd = 0x74;
      else
        fps_snd = 0x76;
      break;
    default:
      fps_snd = 0x72;
      break;
  }

  switch (MTCIndex)
  {
    case 0:
      toSend = ( 0x00 + (LTCIn_offset.f & 0xF));
      break;

    case 1:
      toSend = ( 0x10 + ((LTCIn_offset.f & 0xF0) / 16));
      break;

    case 2:
      toSend = ( 0x20 + (LTCIn_offset.s & 0xF));
      break;

    case 3:
      toSend = ( 0x30 + ((LTCIn_offset.s & 0xF0) / 16));
      break;

    case 4:
      toSend = ( 0x40 + (LTCIn_offset.m & 0xF));
      break;

    case 5:
      toSend = ( 0x50 + ((LTCIn_offset.m & 0xF0) / 16));
      break;

    case 6:
      toSend = ( 0x60 + (LTCIn_offset.h & 0xF));
      break;

    case 7:
      toSend = ( fps_snd + ((LTCIn_offset.h & 0xF0) / 16)); // 0x70 = 24 fps // 0x72 = 25 fps // 0x74 = 30df fps // 0x76 = 30 fps
      break;
  }

  //MIDI.sendTimeCodeQuarterFrame(toSend);

  midiMsg[0]=0xf1;
  midiMsg[1]=toSend;
  MIDI.sendSysEx(sizeof(midiMsg),midiMsg,true);


  if (++MTCIndex > 7)
    MTCIndex = 0;
}

#endif

void setup()
{

#if DSER
Serial.begin(115200);
#endif

#if FTSEG

  ANDisp1.begin(DISP1);
  ANDisp2.begin(DISP2);


  ANDisp1.setBrightness(8);
  ANDisp2.setBrightness(8);

  ANDisp1.clear();
  ANDisp2.clear();

  ANDisp1.writeDisplay();
  ANDisp2.writeDisplay();

#endif


  memset(timeCode,0,12);
  memset(timeCode_off,0,12);
  memset(szUB,0,15);
  //memset(prev_timeCode,0,12);
  //memset(prev_timeCode_off,0,12);


#if OLED
  //OLED Init
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
#endif

#if MVA

  bitOne.begin();
  bitZero.begin();

#endif

  noInterrupts();

  //LED
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);

  bit_time = 0;
  valid_tc_word = false;
  ones_bit_count = false;
  tc_sync = false;
  write_tc_out = false;
  drop_frame_flag = false;
  total_bits =  0;
  current_bit =  0;
  sync_count =  0;

  delay (1000);

  pinMode(4,INPUT);

  // TIMER 1 - DECODE SMPTE LTC
  TCCR1A = B00000000; // clear all
  //TCCR3B = B11000010; // ICNC4 noise reduction + ICES4 start on rising edge + CS11 divide by 8
  TCCR1B = B11000010; // ICNC4 noise reduction + ICES4 start on rising edge + CS11 divide by 8
  // TCCR3B = B11000001; // ICNC4 noise reduction + ICES4 start on rising edge + CS11 divide by 8
  TCCR1C = B00000000; // clear all
  TIMSK1 = B00100000; // ICIE4 enable the icp

  TCNT1 = 0; // clear timer4


  // TIMER 4 - SYNCHRONIZE ON QUARTER FRAMES
  TCCR3A = B00000000; // clear all
  TCCR3B = B00000010; // CS11 divide by 8
  TCCR3C = B00000000; // clear all
  TIMSK3 = B00000010; // OCIE5A ENABLE.

  TCNT3 = 0;      // clear timer5
  OCR3A = 256;//20000;  // Max counter 5

  TCCR0A = B00000000;
  TCCR0B = B00000100; // c/256
  
  TIMSK0 = B00000010; //ocf0a
  OCR0A = 64;

  sei();

#if OLED

  display.setTextSize(0);
  display.setFont();
  display.setTextColor(WHITE);
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print ("GREENSIDE PRODUCTIONS");
  display.setCursor(0, 10);
  display.print ("LTC to MTC Converter");
  display.setCursor(0, 20);
  display.print ("Version: ");
  display.setCursor(50, 20);
  display.print( FIRMWARE_VERSION );
  display.setCursor(0, 30);
  display.print ("Waiting on LTC...");
  display.setCursor(0, 40);
  display.print (LTCFrames);

  display.display();

  delay(2000);

  display.clearDisplay();
#endif

#if DMIDI
  MIDI.begin();
#endif

}

// byte waitingOnTC = 1; //v0.2alpha
long dcnt=0;
uint8_t heartbeat=1;
volatile uint8_t running=0;

void loop()
{

  if (write_tc_out && fps>22)
  {
    running = 1;
   // memcpy(prev_timeCode,timeCode,12);
   // memcpy(prev_timeCode_off,timeCode_off,12);
    
    if (drop_frame_flag) {
      sprintf(timeCode, "%02d:%02d:%02d;%02d", LTCIn.h, LTCIn.m, LTCIn.s, LTCIn.f);
      sprintf(timeCode_off, "%02d:%02d:%02d;%02d", LTCIn_offset.h, LTCIn_offset.m, LTCIn_offset.s, LTCIn_offset.f);
    } else {
      sprintf(timeCode, "%02d:%02d:%02d:%02d", LTCIn.h, LTCIn.m, LTCIn.s, LTCIn.f);
      sprintf(timeCode_off, "%02d:%02d:%02d:%02d", LTCIn_offset.h, LTCIn_offset.m, LTCIn_offset.s, LTCIn_offset.f);
    }

#if FTSEG
    sprintf(szDisp,"%02d%02d%02d%02d",LTCIn.h,LTCIn.m,LTCIn.s,LTCIn.f);
#endif

    strncpy(szUB,(const char *)LTCIn.ub,11);
    write_tc_out = false;

#if OLED
    display.clearDisplay();

    //display.setCursor(0, 0);
    //display.setTextColor(BLACK);
    //display.print(prev_timeCode);
    display.setCursor(0, 0);    
    display.setTextColor(WHITE);
    display.print(timeCode);

    //display.setCursor(0, 10);
    //display.setTextColor(BLACK);
    //display.print(prev_timeCode_off);
    display.setCursor(0, 10);
    display.setTextColor(WHITE);
    display.print(timeCode_off);

    //display.setCursor(0, 20);
    //display.print(userBit);

   //if (prev_fps != fps) {
      //display.setCursor(0, 20);
     // display.setTextColor(BLACK);
      //display.print(prev_fps);
      display.setCursor(0, 20);
      display.setTextColor(WHITE);
      display.print(fps);
      display.setCursor(15, 20);
      display.print("fps");  

    //}
    //display.setCursor(0, 30);
    //display.print (LTCFrames);

#if MVA

    display.setCursor(0, 40);
    //display.print (runningAverage(OCR3A));
    display.print(avgBitOne);
    display.setCursor(0, 48);
    display.print(avgBitZero);

#endif

    display.display();

#endif

#if FTSEG
    for (int i=0;i<4;i++) {
      ANDisp1.writeDigitAscii(i,*(szDisp+i),i==1|i==3);
      ANDisp2.writeDigitAscii(i,*(szDisp+4+i),i==1|i==3);
    }
    ANDisp1.writeDisplay();
    ANDisp2.writeDisplay();
#endif

#if DSER
  if (Serial.availableForWrite()) {
   Serial.print(timeCode);
   //Serial.print("     ");
   Serial.print(szUB);
   //Serial.print("     ");
   Serial.println(fps);
  }
#endif

  } else {

#ifdef FTSEG1
    if (!running) {
      strcpy(szDisp,"SYNCH  ");
      for (int i=0;i<4;i++) {
        ANDisp1.writeDigitAscii(i,*(szDisp+i));
        ANDisp2.writeDigitAscii(i,*(szDisp+4+i));
      }
      ANDisp1.writeDisplay();
      ANDisp2.writeDisplay();

    }
#endif    
  }



  //display.setCursor(0,50);
//  sprintf(szBuf,"%u,%u,%u,%u,%u\n",bit_time_one,bit_time_zero,bit_time,iInsideVector,dcnt);
//  Serial.print(szBuf);

#if READINGS
    int *readings = bitZero.getReadings();  // returns a pointer to the readings
    int n = bitZero.getCount();             // returns the number of readings
    Serial.print("b0 There are ");
    Serial.print(n);
    Serial.println(" readings:");
    for (int i=0; i<n; i++) {
        Serial.println(*readings++);
    }

    int *readings1 = bitOne.getReadings();  // returns a pointer to the readings
    int n1 = bitOne.getCount();             // returns the number of readings
    Serial.print("b1 There are ");
    Serial.print(n1);
    Serial.println(" readings:");
    for (int i=0; i<n; i++) {
        Serial.println(*readings1++);
    }

#endif

}


