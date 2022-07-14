#ifndef LTCCALC_H
#define LTCCALC_H

#include <Arduino.h>
struct smpte_frame_struct_v {

 volatile byte h;
 volatile byte m;
 volatile byte s;
 volatile byte f;
 volatile char ub[11];
    volatile byte fps;
    volatile boolean drop_frame;

};// smpte_frame;

struct smpte_frame_struct {

  byte h;
  byte m;
  byte s;
  byte f;
  char ub[11];
  byte fps;
  boolean drop_frame;

};

void frame2dftc( struct smpte_frame_struct *,long frame, byte framerate);
long dftc2frame( struct smpte_frame_struct *, byte frame_rate);
long tc2frame( struct smpte_frame_struct *, byte fps, boolean drop_frame);
void frame2tc( struct smpte_frame_struct *, long frame, byte fps, boolean drop_frame);

#endif //LTCCALC_H
