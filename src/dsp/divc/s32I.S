#include "dsps_addc_platform.h"
#include "esp_opt.h"

#define in_addr   a2
#define out_addr  a3
#define len       a4
#define C         a5
#define step_in   a6
#define step_out  a7
#define bit_msk   a8

#define in_v      q1
#define out_v     q2

    .text
    .align  ALIGNMENT
    .global dsps_cdiv_s32_esp
    .type   dsps_cdiv_s32_esp,@function

dsps_cdiv_s32_esp: 
// input    - a2
// output   - a3
// len      - a4
// C        - a5
// step_in  - a6
// step_out - a7

  entry	a1, 16

  beqi step_in, 1, no_step_mode
step_mode:
  slli step_in, step_in, 2
  slli step_out, step_out, 2
  loopgtz len, return_success
    l32i a8, in_addr, 0          // Load next data
    quos  a9, C, a8               // Store the multiplication in the a9
    s32i a9, out_addr, 0         // Store result in the output memory

    add in_addr, in_addr, step_in
    add out_addr, out_addr, step_out

no_step_mode:
  bgei step_out, 2, step_mode
  movi.n bit_msk, 0b11
  and    bit_msk, bit_msk, len
  srli   len, len, 2
  beqz   bit_msk, loop_begin
  ee.vld.128.ip in_v, in_addr, 16      // load input
  ee.movi.32.a in_v, a8, 0             // Move first operand
  ee.movi.32.a in_v, a9, 1             // Move second operand
  ee.movi.32.a in_v, a10, 2            // Move third operand
  ee.movi.32.a in_v, a11, 3            // Move fourth operand
  quos a8, C, a8                       // Store the multiplication in the a8
  quos a9, C, a9                       // Store the multiplication in the a9
  quos a10, C, a10                     // Store the multiplication in the a10
  quos a11, C, a11                     // Store the multiplication in the a11
  ee.movi.32.q out_v, a8, 0            // Move result of the first multiplication
  ee.movi.32.q out_v, a9, 1            // Move result of the second multiplication
  ee.movi.32.q out_v, a10, 2           // Move result of the third multiplication
  ee.movi.32.q out_v, a11, 3           // Move result of the fourth multiplication
  ee.vst.128.ip out_v, out_addr, 16    // store results
loop_begin:
  loopgtz len, return_success
    ee.vld.128.ip in_v, in_addr, 16      // load input
    ee.movi.32.a in_v, a8, 0             // Move first operand
    ee.movi.32.a in_v, a9, 1             // Move second operand
    ee.movi.32.a in_v, a10, 2            // Move third operand
    ee.movi.32.a in_v, a11, 3            // Move fourth operand
    quos a8, C, a8                       // Store the multiplication in the a8
    quos a9, C, a9                       // Store the multiplication in the a9
    quos a10, C, a10                     // Store the multiplication in the a10
    quos a11, C, a11                     // Store the multiplication in the a11
    ee.movi.32.q out_v, a8, 0            // Move result of the first multiplication
    ee.movi.32.q out_v, a9, 1            // Move result of the second multiplication
    ee.movi.32.q out_v, a10, 2           // Move result of the third multiplication
    ee.movi.32.q out_v, a11, 3           // Move result of the fourth multiplication
    ee.vst.128.ip out_v, out_addr, 16    // store results
return_success:
  movi.n	in_addr, 0  //
  retw.n              // return status ESP_OK