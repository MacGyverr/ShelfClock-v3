#ifndef SHELF_CLOCK_LED_MAPS_H
#define SHELF_CLOCK_LED_MAPS_H

#include "ShelfClockConfig.h"
#include <stdint.h>

#define digit0 seg(0), seg(1), seg(2), seg(3), seg(4), seg(5), seg(6)
#define fdigit1 seg(2), seg(7), seg(10), seg(15), seg(8), seg(3), seg(9)
#define digit2 seg(10), seg(11), seg(12), seg(13), seg(14), seg(15), seg(16)
#define fdigit3 seg(12), seg(17), seg(20), seg(25), seg(18), seg(13), seg(19)
#define digit4 seg(20), seg(21), seg(22), seg(23), seg(24), seg(25), seg(26)
#define fdigit5 seg(22), seg(27), seg(30), seg(35), seg(28), seg(23), seg(29)
#define digit6 seg(30), seg(31), seg(32), seg(33), seg(34), seg(35), seg(36)

#if LEDS_PER_SEGMENT == 1
 #define seg(n) n*LEDS_PER_SEGMENT
 #define nseg(n) n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 2
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1
 #define nseg(n) n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 3
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2
 #define nseg(n) n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 4
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3
 #define nseg(n) n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 5
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4
 #define nseg(n) n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 6
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+5
 #define nseg(n) n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 7
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+6
 #define nseg(n) n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 8
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+7
 #define nseg(n) n*LEDS_PER_SEGMENT+7, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 9
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+7, n*LEDS_PER_SEGMENT+8
 #define nseg(n) n*LEDS_PER_SEGMENT+8, n*LEDS_PER_SEGMENT+7, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#elif LEDS_PER_SEGMENT == 10
 #define seg(n) n*LEDS_PER_SEGMENT, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+7, n*LEDS_PER_SEGMENT+8, n*LEDS_PER_SEGMENT+9
 #define nseg(n) n*LEDS_PER_SEGMENT+9, n*LEDS_PER_SEGMENT+8, n*LEDS_PER_SEGMENT+7, n*LEDS_PER_SEGMENT+6, n*LEDS_PER_SEGMENT+5, n*LEDS_PER_SEGMENT+4, n*LEDS_PER_SEGMENT+3, n*LEDS_PER_SEGMENT+2, n*LEDS_PER_SEGMENT+1, n*LEDS_PER_SEGMENT
#else
 #error "Not supported Leds per segment. You need to add definition of seg(n) with needed number of elements according to formula above"
#endif

#if HAS_SOUNDDETECTOR
  #define bar0 seg(32), seg(33)
  #define bar1 nseg(30), nseg(35)
  #define bar2 seg(22), seg(23)
  #define bar3 nseg(20), nseg(25)
  #define bar4 seg(12), seg(13)
  #define bar5 nseg(10), nseg(15)
  #define bar6 seg(2), seg(3)
  #define bar7 nseg(0), nseg(5)
  const uint16_t ANALYZER[(NUMBER_OF_DIGITS+1)*LEDS_PER_SEGMENT*2] = {bar0, bar1, bar2, bar3, bar4, bar5, bar6, bar7};
#endif

const uint16_t FAKE_LEDs[FAKE_NUM_LEDS] = {digit0, fdigit1, digit2, fdigit3, digit4, fdigit5, digit6};

//fake LED layout for spectrum (from the middle out)
const uint16_t FAKE_LEDs_C_BMUP[SEGMENTS_LEDS] = {seg(17), seg(11), seg(21), seg(12), seg(20), seg(19), seg(27), seg(7), seg(22), seg(10), seg(26), seg(16), seg(25), seg(13), seg(18), seg(1), seg(31), seg(2), seg(30), seg(9), seg(29), seg(15), seg(23), seg(14), seg(24), seg(0), seg(32), seg(6), seg(36), seg(3), seg(35), seg(8), seg(28), seg(5), seg(33), seg(4), seg(34)};
//fake LED layout for spectrum (bfrom the outside in)
const uint16_t FAKE_LEDs_C_CMOT[SEGMENTS_LEDS] = {seg(19), seg(26), seg(16), seg(20), seg(13), seg(25), seg(12), seg(17), seg(18), seg(21), seg(14), seg(24), seg(11), seg(29), seg(9), seg(22), seg(15), seg(23), seg(10), seg(28), seg(7), seg(27), seg(8), seg(36), seg(6), seg(30), seg(3), seg(35), seg(2), seg(34), seg(1), seg(31), seg(4), seg(32), seg(5), seg(33), seg(0)};
//fake LED layout for spectrum (bottom-left to right-top)
const uint16_t FAKE_LEDs_C_BLTR[SEGMENTS_LEDS] = {seg(32), seg(31), seg(27), seg(30), seg(36), seg(33), seg(34), seg(35), seg(29), seg(22), seg(21), seg(17), seg(20), seg(26), seg(23), seg(28), seg(24), seg(25), seg(19), seg(12), seg(11), seg(7), seg(10), seg(16), seg(13), seg(18), seg(14), seg(15), seg(9), seg(2), seg(1), seg(0), seg(6), seg(3), seg(8), seg(4), seg(5)};
//fake LED layout for spectrum (top-left to bottom-right)
const uint16_t FAKE_LEDs_C_TLBR[SEGMENTS_LEDS] = {seg(34), seg(33), seg(32), seg(36), seg(35), seg(28), seg(24), seg(23), seg(29), seg(30), seg(31), seg(27), seg(22), seg(26), seg(25), seg(18), seg(14), seg(13), seg(19), seg(20), seg(21), seg(17), seg(12), seg(16), seg(15), seg(8), seg(4), seg(3), seg(9), seg(10), seg(11), seg(7), seg(2), seg(6), seg(5), seg(0), seg(1)};
//fake LED layout for spectrum (top-middle down)
const uint16_t FAKE_LEDs_C_TMDN[SEGMENTS_LEDS] = {seg(18), seg(14), seg(24), seg(13), seg(25), seg(19), seg(8), seg(28), seg(15), seg(23), seg(16), seg(26), seg(12), seg(20), seg(17), seg(4), seg(34), seg(3), seg(35), seg(9), seg(29), seg(10), seg(22), seg(11), seg(21), seg(5), seg(33), seg(6), seg(36), seg(2), seg(30), seg(7), seg(27), seg(0), seg(32), seg(1), seg(31)};
//fake LED layout for spectrum (center-sides in)
const uint16_t FAKE_LEDs_C_CSIN[SEGMENTS_LEDS] = {seg(5), seg(32), seg(0), seg(33), seg(6), seg(36), seg(4), seg(31), seg(1), seg(34), seg(3), seg(30), seg(2), seg(35), seg(9), seg(29), seg(8), seg(27), seg(7), seg(28), seg(15), seg(22), seg(10), seg(23), seg(16), seg(26), seg(14), seg(21), seg(11), seg(24), seg(13), seg(20), seg(12), seg(25), seg(19), seg(18), seg(17)};
//fake LED layout for spectrum (bottom-right to left-top)
const uint16_t FAKE_LEDs_C_BRTL[SEGMENTS_LEDS] = {seg(0), seg(1), seg(7), seg(2), seg(6), seg(5), seg(4), seg(3), seg(9), seg(10), seg(11), seg(17), seg(12), seg(16), seg(15), seg(8), seg(14), seg(13), seg(19), seg(20), seg(21), seg(27), seg(22), seg(26), seg(25), seg(18), seg(24), seg(23), seg(29), seg(30), seg(31), seg(32), seg(36), seg(35), seg(28), seg(34), seg(33)};
//fake LED layout for spectrum (top-right to bottom-left)
const uint16_t FAKE_LEDs_C_TRBL[SEGMENTS_LEDS] = {seg(4), seg(5), seg(0), seg(6), seg(3), seg(8), seg(14), seg(15), seg(9), seg(2), seg(1), seg(7), seg(10), seg(16), seg(13), seg(18), seg(24), seg(25), seg(19), seg(12), seg(11), seg(17), seg(20), seg(26), seg(23), seg(28), seg(34), seg(35), seg(29), seg(22), seg(21), seg(27), seg(30), seg(36), seg(33), seg(32), seg(31)};
//fake LED layout for spectrum (horizontal parts)
const uint16_t FAKE_LEDs_C_VERT[SEGMENTS_LEDS] = {seg(17), seg(21), seg(11), seg(27), seg(7), seg(31), seg(1), seg(20), seg(12), seg(22), seg(10), seg(30), seg(2), seg(32), seg(0), seg(19), seg(26), seg(16), seg(29), seg(9), seg(36), seg(6), seg(25), seg(13), seg(23), seg(15), seg(35), seg(33), seg(33), seg(5), seg(18), seg(24),seg(14), seg(28), seg(8), seg(34), seg(4)};
const uint16_t FAKE_LEDs_C_VERT2[SEGMENTS_LEDS] = {seg(18), seg(14), seg(24), seg(8), seg(28), seg(4), seg(34), seg(13), seg(25), seg(15), seg(23), seg(3), seg(35), seg(5), seg(33), seg(19), seg(16), seg(26), seg(9), seg(29), seg(6), seg(36), seg(12), seg(20), seg(10), seg(22), seg(2), seg(30), seg(0), seg(32), seg(17), seg(11),seg(21), seg(7), seg(27), seg(1), seg(31)};
//fake LED layout for spectrum (vertical parts)
const uint16_t FAKE_LEDs_C_OUTS[SEGMENTS_LEDS] = {seg(32), seg(33), seg(36), seg(31), seg(34), seg(30), seg(35), seg(29), seg(27), seg(28), seg(22), seg(23), seg(26), seg(21), seg(24), seg(20), seg(25), seg(19), seg(17), seg(18), seg(12), seg(13), seg(16), seg(11), seg(14), seg(10), seg(15), seg(9), seg(7), seg(8), seg(2), seg(3), seg(6), seg(1), seg(4), seg(0), seg(5)};
const uint16_t FAKE_LEDs_C_OUTS2[SEGMENTS_LEDS] = {seg(0), seg(5), seg(6), seg(1), seg(4), seg(2), seg(3), seg(9), seg(7), seg(8), seg(10), seg(15), seg(16), seg(11), seg(14), seg(12), seg(13), seg(19), seg(17), seg(18), seg(20), seg(25), seg(26), seg(21), seg(24), seg(22), seg(23), seg(29), seg(27), seg(28), seg(30), seg(35), seg(36), seg(31), seg(34), seg(32), seg(33)};

//fake LED layout for fire display
const uint16_t FAKE_LEDs_C_FIRE[SEGMENTS_LEDS] = {seg(17), seg(11), seg(21), seg(12), seg(20), seg(19), seg(27), seg(7), seg(22), seg(10), seg(26), seg(16), seg(25), seg(13), seg(18), seg(1), seg(31), seg(2), seg(30), seg(9), seg(29), seg(15), seg(23), seg(14), seg(24), seg(0), seg(32), seg(6), seg(36), seg(3), seg(35), seg(8), seg(28), seg(5), seg(33), seg(4), seg(34)};
//fake LED layout for Rain display
const uint16_t FAKE_LEDs_C_RAIN[SEGMENTS_LEDS] = {seg(30), seg(35), seg(1), seg(6), seg(4), seg(22), seg(23), seg(31), seg(36), seg(34), seg(12), seg(13), seg(7), seg(9), seg(8), seg(0), seg(5), seg(17), seg(19), seg(18), seg(10), seg(15), seg(21), seg(26), seg(24), seg(2), seg(3), seg(27), seg(29), seg(28), seg(20), seg(25), seg(11), seg(16), seg(14), seg(32), seg(33)};
//fake LED layout for Snake display
const uint16_t FAKE_LEDs_SNAKE[SEGMENTS_LEDS] = {seg(0), seg(1), seg(7), seg(2), seg(6), seg(5), seg(4), seg(3), seg(9), seg(10), seg(11), seg(17), seg(12), seg(16), seg(15), seg(8), seg(14), seg(13), seg(19), seg(20), seg(21), seg(27), seg(22), seg(26), seg(25), seg(18), seg(24), seg(23), seg(29), seg(30), seg(31), seg(32), seg(36), seg(35), seg(28), seg(34), seg(33)};

#endif
