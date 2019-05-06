/* This program reads the array of gas sensors connected to the arduino and loads it into a 
 *  JSON variable to transmit serially to NodeMCU.
 * 
 */

#define   MQ_SENSOR_ANALOG_PIN         0  //define which analog input channel you are going to use- on the ESP8266 there is only one

#define         RL_VALUE                     5300     //define the load resistance on the board, in kilo ohms
#define         RO_CLEAN_AIR_FACTOR          9.83  //RO_CLEAR_AIR_FACTOR=(Sensor resistance in clean air)/RO,
                                                     //which is derived from the chart in datasheet
/***********************Software Related Macros************************************/
#define         CALIBARAION_SAMPLE_TIMES     50    //define how many samples you are going to take in the calibration phase
#define         CALIBRATION_SAMPLE_INTERVAL  5   //define the time interal(in milisecond) between each samples in the
                                                     //cablibration phase
#define         READ_SAMPLE_INTERVAL         50    //define how many samples you are going to take in normal operation
#define         READ_SAMPLE_TIMES            5     //define the time interal(in milisecond) between each samples in 
                                                     //normal operation
#define         GAS_LPG                      0
#define         GAS_CO                       1
#define         GAS_PM10                     2
#define         GAS_CH4                      3
#define         GAS_NH4                      4
//static const char *GAS_ENUM[5]            ={"LPG", "CO", "PM10", "CH4", "NH4"};


/*   Parameters to model temperature and humidity dependence
*/
#define     CORA	0.00035
#define     CORB	0.02718
#define     CORC	1.39538
#define	    CORD	0.0018
#define     CORE	0.003333333
#define     CORF	-0.001923077
#define     CORG	1.130128205

                                                                                            
extern float Ro;    // this has to be tuned 10K Ohm
extern float co_val;
extern float lpg_val;
extern float pm10_val;
extern float methane_val ;
extern float nh4_val;
extern uint8_t air_quality_val;

void MQInit();

void MQGetReadings(float temparature, float humidity);



/****************** MQResistanceCalculation **************************************** 
Input: raw_adc - raw value read from adc, which represents the voltage Output: the calculated sensor resistance Remarks: The sensor and the load resistor forms 
Input:   raw_adc - raw value read from adc, which represents the voltage
Output:  the calculated sensor resistance
Remarks: The sensor and the load resistor forms a voltage divider. Given the voltage
         across the load resistor and its resistance, the resistance of the sensor
         could be derived.
************************************************************************************/ 
float MQResistanceCalculation(int raw_adc);

/***************************** MQCalibration ****************************************
Input:   mq_pin - analog channel
Output:  Ro of the sensor
Remarks: This function assumes that the sensor is in clean air. It use  
         MQResistanceCalculation to calculates the sensor resistance in clean air 
         and then divides it with RO_CLEAN_AIR_FACTOR. RO_CLEAN_AIR_FACTOR is about 
         10, which differs slightly between different sensors.
************************************************************************************/ 
float MQCalibration(int mq_pin);

/*****************************  MQRead *********************************************
Input:   mq_pin - analog channel
Output:  Rs of the sensor
Remarks: This function use MQResistanceCalculation to caculate the sensor resistenc (Rs).
         The Rs changes as the sensor is in the different consentration of the target
         gas. The sample times and the time interval between samples could be configured
         by changing the definition of the macros.
************************************************************************************/ 
float MQRead(int mq_pin);


/*****************************  MQGetGasPercentage **********************************
Input:   rs_ro_ratio - Rs divided by Ro
         gas_id      - target gas type
Output:  ppm of the target gas
Remarks: This function passes different curves to the MQGetPercentage function which 
         calculates the ppm (parts per million) of the target gas.
************************************************************************************/ 
int MQGetGasPercentage(float rs_ro_ratio, int gas_id);

/*****************************  MQGetPercentage **********************************
Input:   rs_ro_ratio - Rs divided by Ro
         pcurve      - pointer to the curve of the target gas
Output:  ppm of the target gas
Remarks: USing the a * b coeffients in the equation a*x^b the ppm value is derived
************************************************************************************/ 
int  MQGetPercentage(float rs_ro_ratio, float *pcurve);
