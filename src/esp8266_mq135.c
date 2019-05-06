/* This program reads the array of gas sensors connected to the arduino and loads it into a 
 *  JSON variable to transmit serially to NodeMCU.
 * 
 */

#include <stdio.h>
#include <math.h>
#include "esp8266_mq135.h"
#include <espressif/esp_common.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>



float Ro = 41763.0;    // this has to be tuned 10K Ohm
float co_val = 0;
float lpg_val = 0;
float pm10_val = 0;
float methane_val = 0;
float nh4_val = 0;
uint8_t air_quality_val = 0;


unsigned long SLEEP_TIME = 50000; // Sleep time between reads (in milliseconds)

float Rs = 0;           // variable to store the value coming from the sensor
float valMQ =0.0;
float lastMQ =0.0;

/* Values derived from exponential regression of respective gas datapoints from the datasheet.
 *  The values represent the a & b values in the a*x^b
 */
float           LPGCurve[2]  =  {632.8357,-2.025202};   //two points are taken from the curve.
                                                    //with these two points, a line is formed which is "approximately equivalent"
                                                    //to the original curve.
                                                    //data format:{ x, y, slope}; point1: (lg200, 0.21), point2: (lg10000, -0.59)

float           COCurve[2] = {116.6020682, -2.769034857};
float           PM10Curve[2] ={3896.4,-1.886306};    //two points are taken from the curve.
                                                    //with these two points, a line is formed which is "approximately equivalent"
                                                    //to the original curve.
                                                    //data format:{ x, y, slope}; point1: (lg200, 0.53), point2:(lg10000,-0.22)

float           CH4Curve[2]    = {4084.538, -2.410099};

float           NH4Curve[2]    = {102.2348, -2.554241};


void MQInit(){

   Ro = MQCalibration(MQ_SENSOR_ANALOG_PIN);         //Calibrating the sensor. Please make sure the sensor is in clean air 
   printf ("calibrated value for Ro is %f\n", Ro);   //when you perform the calibration  
}


float get_correction_factor(float temperature, float humidity){

        /* Calculates the correction factor for ambient air temperature and relative humidity
        * Based on the linearization of the temperature dependency curve
        * under and above 20 degrees Celsius, asuming a linear dependency on humidity,
        * provided by Balk77 https://github.com/GeorgK/MQ135/pull/6/files
        */


        if (temperature < 20){
            return CORA * temperature * temperature - CORB * temperature + CORC - (humidity - 33.0) * CORD;
	}
	else {
        return CORE * temperature + CORF * humidity + CORG;
	}
}


void MQGetReadings(float temperature, float humidity){
  
	float rs_ro_ratio=0;
	float correction_factor=0;

	Rs = MQRead(MQ_SENSOR_ANALOG_PIN);
	correction_factor = get_correction_factor(temperature, humidity);
	Rs = Rs / correction_factor;
	rs_ro_ratio = Rs/Ro;
  	printf("RS_RO Ratio is %f\n", rs_ro_ratio);
	co_val = MQGetGasPercentage(rs_ro_ratio,GAS_CO);
  	lpg_val = MQGetGasPercentage(rs_ro_ratio,GAS_LPG);
   	pm10_val = MQGetGasPercentage(rs_ro_ratio,GAS_PM10);
   	methane_val = MQGetGasPercentage(rs_ro_ratio,GAS_CH4);
	nh4_val = MQGetGasPercentage(rs_ro_ratio,GAS_NH4);

	air_quality_val =0;

	if (co_val >=15.4 && air_quality_val < 5){
		air_quality_val = 5;
	} else if (co_val >= 12.5 && air_quality_val < 4){
		air_quality_val = 4;
        } else if (co_val >= 9.5 && air_quality_val < 3){
                air_quality_val = 3;
        } else if (co_val >= 4.5 && air_quality_val < 2){
                air_quality_val = 2;  
        } else if (air_quality_val == 0){
                air_quality_val = 1;    
	}
		
        if (pm10_val >= 355 && air_quality_val < 5){
                air_quality_val = 5;
        } else if (pm10_val >= 255 && air_quality_val < 4){
                air_quality_val = 4;
        } else if (pm10_val >= 155 && air_quality_val < 3){
                air_quality_val = 3;
        } else if (pm10_val >= 55 && air_quality_val < 2){
                air_quality_val = 2;
        } else if (air_quality_val == 0){
                air_quality_val = 1;
        }

	printf("corrcetion factor %f, air_quality_val %i, Rs %f, CO %f, LPG %f, PM10 %f, methane %f, NH4 %f\n", correction_factor, air_quality_val, Rs, co_val, lpg_val, pm10_val, methane_val, nh4_val);
}



/****************** MQResistanceCalculation **************************************** 
Input:   raw_adc - raw value read from adc, which represents the voltage
Output:  the calculated sensor resistance
Remarks: The sensor and the load resistor forms a voltage divider. Given the voltage
         across the load resistor and its resistance, the resistance of the sensor
         could be derived.
************************************************************************************/ 
float MQResistanceCalculation(int raw_adc)
{
  printf("RS value is %i\n", raw_adc);
  return ( ((float)RL_VALUE*(1024-raw_adc)/raw_adc));
}

/***************************** MQCalibration ****************************************
Input:   mq_pin - analog channel
Output:  Ro of the sensor
Remarks: This function assumes that the sensor is in clean air. It use  
         MQResistanceCalculation to calculates the sensor resistance in clean air 
         and then divides it with RO_CLEAN_AIR_FACTOR. RO_CLEAN_AIR_FACTOR is about 
         10, which differs slightly between different sensors.
************************************************************************************/ 
float MQCalibration(int mq_pin)
{
  int i;
  float val=0;
 
  for (i=0;i<CALIBARAION_SAMPLE_TIMES;i++) {            //take multiple samples
    val += MQResistanceCalculation(sdk_system_adc_read());
    vTaskDelay(CALIBRATION_SAMPLE_INTERVAL);
  }
  val = val/CALIBARAION_SAMPLE_TIMES;                   //calculate the average value
  printf("Calibrated Value is %f\n", val);
  val = val/RO_CLEAN_AIR_FACTOR;                        //divided by RO_CLEAN_AIR_FACTOR yields the Ro 
                                                        //according to the chart in the datasheet 
  
  return val; 
}

/*****************************  MQRead *********************************************
Input:   mq_pin - analog channel
Output:  Rs of the sensor
Remarks: This function use MQResistanceCalculation to caculate the sensor resistenc (Rs).
         The Rs changes as the sensor is in the different consentration of the target
         gas. The sample times and the time interval between samples could be configured
         by changing the definition of the macros.
************************************************************************************/ 
float MQRead(int mq_pin)
{
  int i;
  float rs=0;
 
  for (i=0;i<READ_SAMPLE_TIMES;i++) {
    rs += MQResistanceCalculation(sdk_system_adc_read());
    vTaskDelay(READ_SAMPLE_INTERVAL);
  }
 
  rs = rs/READ_SAMPLE_TIMES;	
  
  return rs;  
}
/*****************************  MQGetGasPercentage **********************************
Input:   rs_ro_ratio - Rs divided by Ro
         gas_id      - target gas type
Output:  ppm of the target gas
Remarks: This function passes different curves to the MQGetPercentage function which 
         calculates the ppm (parts per million) of the target gas.
************************************************************************************/ 
int MQGetGasPercentage(float rs_ro_ratio, int gas_id)
{
  if ( gas_id == GAS_LPG ) {
     return MQGetPercentage(rs_ro_ratio,LPGCurve);
  } else if ( gas_id == GAS_CO ) {
     return MQGetPercentage(rs_ro_ratio,COCurve);
  } else if ( gas_id == GAS_PM10 ) {
     return MQGetPercentage(rs_ro_ratio,PM10Curve);
  } else if ( gas_id == GAS_CH4 ) {
     return MQGetPercentage(rs_ro_ratio,CH4Curve); 
  } else if ( gas_id == GAS_NH4 ) {
     return MQGetPercentage(rs_ro_ratio,NH4Curve);
  }
     
 
  return 0;
}

/*****************************  MQGetPercentage **********************************
Input:   rs_ro_ratio - Rs divided by Ro
         pcurve      - pointer to the curve of the target gas
Output:  ppm of the target gas
Remarks: USing the a * b coeffients in the equation a*x^b the ppm value is derived
************************************************************************************/ 
int  MQGetPercentage(float rs_ro_ratio, float *pcurve)
{
  return pcurve[0] * powf(rs_ro_ratio, pcurve[1]);
}
