#include "ltccalc.h"


/**
   
   Simple timecode calculator for gDocs spreadshet. Heavily inspired by Nuke/Hiero Timecode Calculator Panel v1 by Matt Brealey. No support for obscure non even frame rates (yet).
   User have to have some sense since with the input we're not throwing errors for all odd cases possible.

   Drop frame support with the help of David Heidelberger's blog, http://www.davidheidelberger.com/blog/?p=29. David based his work on Andrew Duncan's math, http://andrewduncan.net/timecodes/.

   Works with both manually entered data and cell references.

   Takes two timecodes, framerate and an operation as input. =tc(tc1, tc2, fps, operation)
   tc1 = The first timecode, always a string or cell/range reference. E.g. "10:08:10:01", H34 or H34:H56.
   tc2 = The second timecode. String or cell/range reference for addition and subtraction, e.g. "00:01:04:15", G34 or G34:G56. For all other operations integer or float, e.g. 5.4. When converting between framerates with "conv" this is the new target framerate, e.g. 24.
   fps = source framerate. Always integer. E.g. 24
   operation = Any of "add", "sub", "div", "mult", "per", "conv", "2tc", "2f". E.g. "add".  All modes not that usefull...

   supported framerates: Any integer frame rate such as 24, 25, 30, 60. 29.97, "29.97df", 59.94, "59.94df". You must type "" around the dropframe framerates.


   "add"  : tc1 + tc2. Both TC formatted as HH:MM:SS:FF. E.g ("00:08:43:12", "00:07:12:03", 24, "add")
   "sub"  : tc1 - tc2. Both TC formatted as HH:MM:SS:FF. E.g ("05:04:13:09", "00:00:08:23", 25, "sub")
   "div"  : tc1 / tc2. tc2 in this case is the divisor as an integer or float, NOT HH:MM:SS:FF. E.g ("05:04:13:09", 2, 24, "div")
   "mult" : tc1 * tc2. tc2 in this case is the multiplier as an integer or float, NOT HH:MM:SS:FF. E.g ("12:45:32:23", 5, 24, "mult")
   "per"  : tc1 * (tc2/100). tc2 in this case should be an int or float, NOT HH:MM:SS:FF.
   "conv" : will convert tc1 to the framerate given as tc2. tc2 in this case should be target framerate as an integer. E.g ("00:12:30:11", 25, 24, "conv")
   "2tc"  : will convert tc1, here given as frames, to timecode. Value for tc2 is ignored and is best left blank.
   "2f"   : will convert tc1 to frames. Value for tc2 is ignored and is best left blank.

   I really don't know this shit so feel free to come with suggestions and feedback. =)
   @version v0.6
   @author Henrik Cednert, neo@irry.com, @NEO_AMiGA

   Changelog
   2014-06-17, v0.3.   Conversion of tc had the framerates mixed up.
   2014-06-19, v0.35.  Added autocomplete and help.
   2014-06-27, v0.351. Conversion of frames to tc and vice versa added.
   2014-09-21, v0.5.   Combined the experimental version that works with ranges and the original single cell based version. Hope I didn't break anything, let me know if I did. =)
   2014-09-22, v0.52.  29.97, 29.97 drop frame, 59.94 and 59.94 drop frame support. Not 23.976 at this time since that's a fictional thing... =)
   2015-03-29, v0.6.   Rewrote loads of the code. Had to remove what was started with inter-column calculations for now. Made sure drop frame works.

*/

/**
   A simple timecode calculator capable of adding, subtracting, dividing, multipling, percentage operations, conversion between framerates, conversion from frames to tc and vice versa.
    Now works with ranges, e.g: =tc(B36:B42, ,24,"2f").


   @param {"10:08:10:01"} tc1 The first timecode, always a string or cell/range reference. E.g. "10:08:10:01", H34 or H34:H56
   @param {"00:01:04:15"} tc2 The second timecode. String or cell/range reference for addition and subtraction, e.g. "00:01:04:15", G34 or G34:G56. For all other operations integer or float, e.g. 5.4. When converting between framerates with "conv" this is the new target framerate, e.g. 24.
   @param {24} fps Source framerate. E.g, 24, 25, 30, 60, 23.976, 23.98, 29.97, 59.94, "29.97df", "59.94df"
   @param {"add"} operation Any of "add", "sub", "div", "mult", "per", "conv", "2tc", "2f". E.g. "add"
   @customfunction
*/






/*
  CONVERT A FRAME NUMBER TO DROP FRAME TIMECODE
  Code by David Heidelberger (http://www.davidheidelberger.com/blog/?p=29), adapted from Andrew Duncan (http://andrewduncan.net/timecodes/).
  Massaged to work with gDocs by Henrik Cednert. Intentionally kept code as true as possible to David's instead of merging into mine.
  Given an int called framenumber and a double called framerate
  Framerate should be 29.97, 59.94, or 23.976, otherwise the calculations will be off.
*/
//void frame2dftc(byte tcOut[], long frame, byte framerate) { // note this is not a non-integer rate representation.
void frame2dftc(struct smpte_frame_struct * tcOut, long frame, byte framerate) { // note this is not a non-integer rate representation.

  //static byte tcOut[4] = {0, 0, 0, 0};

  long framenumber = frame;

  long dropFrames = round(framerate * .066666);                  //Number of frames to drop on the minute marks is the nearest integer to 6% of the framerate
  long framesPerHour =  round((long)framerate * 3600);              //Number of frames in an hour
  long framesPer24Hours = framesPerHour * 24;                         //Number of frames in a day - timecode rolls over after 24 hours
  long framesPer10Minutes =  round((long)framerate * 600);         //Number of frames per ten minutes
  long framesPerMinute = ( round((long)framerate) * 60) -  dropFrames; //Number of frames per minute is the round of the framerate * 60 minus the number of dropped frames

  //Negative time. Add 24 hours.
  if (framenumber < 0) {
    framenumber = framesPer24Hours + framenumber;
  }

  //If framenumber is greater than 24 hrs, next operation will rollover clock. On purpose disabled by Henrik!
  //framenumber = framenumber % framesPer24Hours; //% is the modulus operator, which returns a remainder. a % b = the remainder of a/b

  //d = framenumber\framesPer10Minutes; // \ means integer division, which is a/b without a remainder. Some languages you could use floor(a/b)
  long TenMinCounter = (long)(framenumber / framesPer10Minutes); // \ means integer division, which is a/b without a remainder. Some languages you could use floor(a/b)
  long TenMinCounter_mod = framenumber % framesPer10Minutes;

  //throw "framenumber: " + framenumber + " framesPer10Minutes: "+ framesPer10Minutes+" TenMinCounter: " +TenMinCounter + "  TenMinCounter_mod: " + TenMinCounter_mod + "   dropFrames: " + dropFrames


  //In the original post, the next line read m>1, which only worked for 29.97. Jean-Baptiste Mardelle correctly pointed out that m should be compared to dropFrames.
  if (TenMinCounter_mod > dropFrames) {
    framenumber = framenumber + (dropFrames * 9 * TenMinCounter) + dropFrames * (floor( (TenMinCounter_mod - dropFrames) / framesPerMinute) );
  }

  else {
    framenumber = framenumber + dropFrames * 9 * TenMinCounter;
  }

  byte frRound = round(framerate);
  tcOut->f = framenumber % frRound;
  tcOut->s = ((int)(framenumber / frRound)) % 60;
  tcOut->m = ((int)( (int)(framenumber / frRound) / 60)) % 60;
  tcOut->h = (int)( (int)( (int)(framenumber / frRound) / 60) / 60);


  //return (byte *)tcOut;

}



/*
  CONVERT DROP FRAME TIMECODE TO A FRAME NUMBER
  Code by David Heidelberger, adapted from Andrew Duncan.
  Massaged to work with gDocs by Henrik Cednert. Intentionally kept code as true as possible to David's instead of merging into mine.
  Given ints called hours, minutes, seconds, frames, and a double called framerate
*/
//long dftc2frame(byte tcIn[], byte fps) {
long dftc2frame(struct smpte_frame_struct * tcIn, byte fps) {

  if (fps != 30) return -1;

  byte framerate = fps;

  long dropFrames = round(framerate * .066666); // 2//Number of drop frames is 6% of framerate rounded to nearest integer // 2 or 4 - 4 when FPS=59.976

  byte timeBase = round(framerate);             // 30 //We don't need the exact framerate anymore, we just need it rounded to nearest integer

  long hourFrames = (long) timeBase * 3600;              //Number of frames per hour (non-drop)
  long minuteFrames = (long) timeBase * 60;                 //Number of frames per minute (non-drop)
  long totalMinutes = (60 * (long)tcIn->m) + (long)tcIn->s;        //Total number of minutes
  long frameNumber = ((hourFrames * (long)tcIn->h) + (minuteFrames * (long)tcIn->m) + (timeBase * (long)tcIn->s) + (long)tcIn->f) - (dropFrames * (totalMinutes - floor(totalMinutes / 10)));

  return frameNumber;

}



/*
  TIMECODE TO FRAMES
*/
//long tc2frame(byte tcIn[], byte fps, boolean drop_frame) {
long tc2frame(struct smpte_frame_struct * tcIn, byte fps, boolean drop_frame) {

  long frameOut = 0;

  //Check if we're dealing with decimal separated frame_rate or not.
  //boolean tc_check = fps % 1 ;

  if (drop_frame && (fps!=30)) return -1; //error condition
  // For framerates that should be calculated as integers 24, 25, 30, 29.97, 59.94. The later two rounded to closest integer, 30 and 60.
  // These checks could be simplified but with the current approach I hardcode what's supported and what's not, which in its way is a good thing.
  //if ( (tc_check) || (fps == = 23.976) || (fps == = 23.98) || (fps == = 29.97) || (fps == = 59.94) || ( (operation == = "conv") && (!dropFrame) ) ) {
  if (!drop_frame) {

    frameOut = tcIn->f + ( (tcIn->s + (tcIn->m * 60) + (tcIn->h * 3600) ) * round((long)fps) );

  } else {

    //If it's a drop frame frame rate, convert and round df framerates to sensible framerates
    //else if ((fps == = "29.97df") || (fps == = "59.94df")) {
    //fps = Math.round(fps.slice(0, -2))
    //Calculate framenumber from drop frame timecode
    frameOut = dftc2frame(tcIn, fps);
  }

  return frameOut;

}



/*
  FRAMES TO TIMECODE. Done after calculations.
*/
//void frame2tc(byte tcOut[], long frame, long fps, boolean drop_frame) {
void frame2tc(struct smpte_frame_struct * tcOut, long frame, byte fps, boolean drop_frame) {

  //static byte tcOut[4] = {0, 0, 0, 0};
  static long sOut=0;


    if (frame < 0) {
  		long framesPerHour =  round((long)fps * 3600);              //Number of frames in an hour
  		long framesPer24Hours = framesPerHour * 24;                         //Number of frames in a day - timecode rolls over after 24 hours
	    frame = framesPer24Hours + frame;
	}

  // For framerates that should be calculated as integers 24, 25, 30, 29.97, 59.94. The later two rounded to closest integer, 30 and 60.
  // These checks could be simplified but with the current approach I hardcode what's supported and what's not, which in its way is a good thing.
  if ( !drop_frame ) {

    tcOut->f = frame % fps;
    sOut = floor(frame / fps);
    tcOut->h = floor(  sOut / 3600);
    tcOut->m = floor( (sOut / 60) % 60);
    tcOut->s = (sOut % 60);

  } else {

    frame2dftc(tcOut, frame, fps);

  }
}
