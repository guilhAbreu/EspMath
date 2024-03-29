#include "dsps_addc_platform.h"
#include "esp_opt.h"

#define x_addr    a2
#define y_addr    a3
#define len       a4
#define C         a5
#define step_x    a6
#define step_y    a7
#define frac      a8

#define x_r       a9
#define y_r       a10

#define unligned  a6
#define bit_msk   a7
#define x_v       q0
#define c_v       q1
#define y_v       q2

  .text
  .align  ALIGNMENT
  .global dsps_csub_s16_esp
  .type   dsps_csub_s16_esp,@function

dsps_csub_s16_esp: 
// x        - a2
// y        - a3
// len      - a4
// C        - a5
// step_x   - a6
// step_y   - a7
// frac     - a8 (stack)

  entry	sp, 16

  l32i   frac, a1, 16
  ssr    frac                 // sar = frac
  beqi step_x, 1, no_step_mode
step_mode:
  l32i C, C, 0                   // Load const
  slli  step_x,  step_x, 1
  slli  step_y,  step_y, 1
  loopgtz len, return_success
    l16si x_r, x_addr, 0         // load next data
    sub y_r, C, x_r                // Store the multiplication at a8
    srl y_r, y_r                   // Shift right (sar), and store in a8
    s16i y_r, y_addr, 0         // Store result in the output memory
    
    add  y_addr, y_addr, step_y  // next output;
    add  x_addr, x_addr, step_x  // next input;
no_step_mode:
  bgei  step_y, 2, step_mode
  ee.vldbc.16 c_v, C           // c_v = C
  movi.n bit_msk, 0b111
  and    unligned, bit_msk, len
  srli   len, len, 3
loop_begin:
  loopgtz len, loop_unligned
    ee.zero.qacc                         // clear accumulator
    ee.vld.128.ip x_v, x_addr, 16      // load input
    ee.vsubs.s16 y_v, c_v, x_v        // sub
    ee.vst.128.ip y_v, y_addr, 16    // store results
loop_unligned:
  l32i C, C, 0                   // Load const
  loopgtz unligned, return_success
    l16si x_r, x_addr, 0         // load next data
    sub y_r, C, x_r                // Store the multiplication at a8
    srl y_r, y_r                   // Shift right (sar), and store in a8
    s16i y_r, y_addr, 0         // Store result in the output memory
    
    addi  y_addr, y_addr, 2  // next output;
    addi  x_addr, x_addr, 2  // next input;
return_success:
  movi.n	x_addr, 0  //
  retw.n              // return status ESP_OK