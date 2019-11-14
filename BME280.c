/* Copyright (c) 2017, Stanford University
 * All rights reserved.
 * 
 * The point of contact for the MENTAID wearables dev team is 
 * Jan Liphardt (jan.liphardt@stanford.edu)
 * 
 * The code is modified from a reference implementation by Kris Winer
 * (tleracorp@gmail.com)
 * Copyright (c) 2017, Tlera Corporation
 * The license terms of the TLERACORP material are: 
 * "Library may be used freely and without limit with attribution."
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of STANFORD UNIVERSITY nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY STANFORD UNIVERSITY "AS IS" AND ANY EXPRESS 
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL STANFORD UNIVERSITY OR ITS CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE 
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT 
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "BME280.h"

#define BME280_ADDRESS_1  0x77

#define BME280_ID         0xD0

#define BME280_PRESS_MSB  0xF7
#define BME280_PRESS_LSB  0xF8
#define BME280_PRESS_XLSB 0xF9

#define BME280_TEMP_MSB   0xFA
#define BME280_TEMP_LSB   0xFB
#define BME280_TEMP_XLSB  0xFC

#define BME280_HUM_MSB    0xFD
#define BME280_HUM_LSB    0xFE

#define BME280_CONFIG     0xF5
#define BME280_CTRL_MEAS  0xF4
#define BME280_STATUS     0xF3
#define BME280_CTRL_HUM   0xF2
#define BME280_RESET      0xE0
#define BME280_CALIB00    0x88
#define BME280_CALIB26    0xE1

enum Posr {P_OSR_00 = 0, /* no op */ P_OSR_01, P_OSR_02, P_OSR_04, P_OSR_08, P_OSR_16};
enum Hosr {H_OSR_00 = 0, /* no op */ H_OSR_01, H_OSR_02, H_OSR_04, H_OSR_08, H_OSR_16};
enum Tosr {T_OSR_00 = 0, /* no op */ T_OSR_01, T_OSR_02, T_OSR_04, T_OSR_08, T_OSR_16};
enum IIRFilter {full = 0,  /* bandwidth at full sample rate */ BW0_223ODR, BW0_092ODR, BW0_042ODR, BW0_021ODR /* bandwidth at 0.021 x sample rate */ };
enum Mode {BME280Sleep = 0, forced, forced2, normal};
enum SBy  {t_00_5ms = 0, t_62_5ms, t_125ms, t_250ms, t_500ms, t_1000ms, t_10ms, t_20ms};

// Read and store calibration data
uint8_t calib26[26];
uint8_t calib7[7];
  
//from read PTH
static uint8_t rawData[8];  // 20-bit pressure register data stored here

int32_t result[3];
int32_t var1, var2, t_fine, adc_T;

//from Pressure comp
int32_t varP1, varP2;
uint32_t P;
    
// BME280 compensation parameters
uint8_t  dig_H1, dig_H3, dig_H6;
uint16_t dig_T1, dig_P1, dig_H4, dig_H5;
int16_t  dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9, dig_H2;

//uint32_t delt_t = 0, count = 0, sumCount = 0, slpcnt = 0;  // used to control display output rate

// Specify BME280 configuration
// set pressure and temperature output data rate
//uint8_t Posr = P_OSR_16, Hosr = H_OSR_16, Tosr = T_OSR_02, Mode = normal, IIRFilter = BW0_021ODR, SBy = t_62_5ms; 
/* @EfektaSB */
uint8_t Posr = P_OSR_01, Hosr = H_OSR_01, Tosr = T_OSR_01, Mode = forced, IIRFilter = full, SBy = t_00_5ms;

// * @brief Function for setting active
void BME280_Turn_On(void)
{
    uint8_t e = readByte(BME280_ADDRESS_1, BME280_ID);
    
    NRF_LOG_INFO("BME280 ID:%d Should be = 96\n", e);
                       
    if(e == 0x60) {
        writeByte( BME280_ADDRESS_1, BME280_RESET, 0xB6 ); // reset BME280 before initialization   
        nrf_delay_ms(100);
        BME280_Configure( BME280_ADDRESS_1 ); // Initialize BME280 altimeter
        nrf_delay_ms(100);
    };
   
}

void BME280_Configure( uint8_t address )
{
  // Configure the BME280
    
  // Set H oversampling rate
  writeByte(address, BME280_CTRL_HUM, 0x07 & Hosr);
  
  // Set T and P oversampling rates and sensor mode
  writeByte(address, BME280_CTRL_MEAS, Tosr << 5 | Posr << 2 | Mode);
  
  // Set standby time interval in normal mode and bandwidth
  writeByte(address, BME280_CONFIG, SBy << 5 | IIRFilter << 2);

  readBytes(address, BME280_CALIB00, calib26, 26);
  
  dig_T1 = (uint16_t)(((uint16_t) calib26[ 1] << 8) | calib26[ 0]);
  //NRF_LOG_DEBUG("BME280T1:%d\r\n",dig_T1);
  dig_T2 = ( int16_t)((( int16_t) calib26[ 3] << 8) | calib26[ 2]);
  //NRF_LOG_DEBUG("BME280T2:%d\r\n",dig_T2);
  dig_T3 = ( int16_t)((( int16_t) calib26[ 5] << 8) | calib26[ 4]);
  //NRF_LOG_DEBUG("BME280T3:%d\r\n",dig_T3);
  dig_P1 = (uint16_t)(((uint16_t) calib26[ 7] << 8) | calib26[ 6]);
  //NRF_LOG_DEBUG("BME280P1:%d\r\n",dig_P1);
  dig_P2 = ( int16_t)((( int16_t) calib26[ 9] << 8) | calib26[ 8]);
  dig_P3 = ( int16_t)((( int16_t) calib26[11] << 8) | calib26[10]);
  dig_P4 = ( int16_t)((( int16_t) calib26[13] << 8) | calib26[12]);
  dig_P5 = ( int16_t)((( int16_t) calib26[15] << 8) | calib26[14]);
  dig_P6 = ( int16_t)((( int16_t) calib26[17] << 8) | calib26[16]);
  dig_P7 = ( int16_t)((( int16_t) calib26[19] << 8) | calib26[18]);
  dig_P8 = ( int16_t)((( int16_t) calib26[21] << 8) | calib26[20]);
  dig_P9 = ( int16_t)((( int16_t) calib26[23] << 8) | calib26[22]);
  
  //24 is missing - this is not typo - complain to Bosch
  dig_H1 = calib26[25];

  readBytes(address, BME280_CALIB26, calib7, 7);
  
  dig_H2 = ( int16_t)((( int16_t) calib7[1] << 8) | calib7[0]);
  dig_H3 = calib7[2];
  dig_H4 = ( int16_t)(((( int16_t) calib7[3] << 8) | (0x0F & calib7[4]) << 4) >> 4);
  dig_H5 = ( int16_t)(((( int16_t) calib7[5] << 8) | (0xF0 & calib7[4]) ) >> 4 );
  dig_H6 = calib7[6];
  
}

void BME280_Get_Data(int32_t * resultPTH)
{
  writeByte(BME280_ADDRESS_1, BME280_CTRL_MEAS, Tosr << 5 | Posr << 2 | Mode);
  readBytes(BME280_ADDRESS_1, BME280_PRESS_MSB, rawData, 8);  
  
  //Pressure
  result[0] = (uint32_t) (((uint32_t) rawData[0] << 16 | (uint32_t) rawData[1] << 8 | rawData[2]) >> 4);
  result[1] = (uint32_t) (((uint32_t) rawData[3] << 16 | (uint32_t) rawData[4] << 8 | rawData[5]) >> 4);
  result[2] = (uint16_t) (((uint16_t) rawData[6] <<  8 |            rawData[7]) );
    
  //Need t_fine for all three compensations
  adc_T = result[1];
  
  var1 = (((( adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
  
  t_fine = var1 + var2;
  
  resultPTH[0] = BME280_Compensate_P(result[0], t_fine);
  resultPTH[1] = BME280_Compensate_T(           t_fine);
  resultPTH[2] = BME280_Compensate_H(result[2], t_fine);
  
  NRF_LOG_INFO("BME280:%d %d %d\n", resultPTH[0], resultPTH[1], resultPTH[2]);
       
}  

// Returns humidity in %RH as unsigned 32 bit integer in Q22.10 format (22integer and 10fractional bits).
// Output value of 47445represents 47445/1024= 46.333%RH
uint32_t BME280_Compensate_H(int32_t adc_H, int32_t t_fine)
{
  int32_t varH;
  varH = (t_fine - ((int32_t)76800));
  varH = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) - (((int32_t)dig_H5) * varH)) +
    ((int32_t)16384)) >> 15) * (((((((varH * ((int32_t)dig_H6)) >> 10) * (((varH *
    ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));
  varH = (varH - (((((varH >> 15) * (varH >> 15)) >> 7) * ((int32_t)dig_H1)) >> 4));
  varH = (varH < 0 ? 0 : varH); 
  varH = (varH > 419430400 ? 419430400 : varH);
  return(uint32_t)(varH >> 12);
}

// Returns temperature in DegC, resolution is 0.01 DegC. Output value of
// 5123 equals 51.23 DegC.
int32_t BME280_Compensate_T(int32_t t_fine)
{
  int32_t T;
  //var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  //var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
  //t_fine = var1 + var2;
  T = (t_fine * 5 + 128) >> 8;
  return T;
}

// Returns pressure in Pa as unsigned 32 bit integer. Output value of 96386 equals 96386 Pa = 963.86 hPa
uint32_t BME280_Compensate_P(int32_t adc_P, int32_t t_fine) 
{
    varP1 = (t_fine>>1) - (int32_t)64000;
    varP2 = (((varP1>>2) * (varP1>>2)) >> 11 ) * ((int32_t)dig_P6);
    varP2 = varP2 + ((varP1*((int32_t)dig_P5))<<1);
    varP2 = (varP2>>2)+(((int32_t)dig_P4)<<16);
    varP1 = (((dig_P3 * (((varP1>>2) * (varP1>>2)) >> 13 )) >> 3) + ((((int32_t)dig_P2) * varP1)>>1))>>18; 
    varP1 = ((((32768+varP1))*((int32_t)dig_P1))>>15);
    if (varP1 == 0) 
    {
        return 0; // avoid exception caused by division by zero 
    }
    P = (((uint32_t)(((int32_t)1048576)-adc_P)-(varP2>>12)))*3125; 
    if (P < 0x80000000)
    {
        P = (P << 1) / ((uint32_t)varP1); 
    }
    else
    {
        P = (P / (uint32_t)varP1) * 2;
    }
    varP1 = (((int32_t)dig_P9) * ((int32_t)(((P>>3) * (P>>3))>>13)))>>12; 
    varP2 = (((int32_t)(P>>2)) * ((int32_t)dig_P8))>>13;
    P = (uint32_t)((int32_t)P + ((varP1 + varP2 + dig_P7) >> 4));
    return P;
}


