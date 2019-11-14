# esp-homekit-air-quality-sensor


Uses an MQ135 to sense air quality, and a DHT22 for temperature and humidity to help make the sensor more accurate. 

The MQ135 sensor is capable of providing:-

Air Quality
PM10 density
Carbon monoxide level
LPG (custom characteristic only visbile in Eve)
Methane (custom characteristic only visbile in Eve)
NH$ Ammonium (custom characteristic only visbile in Eve)


It cannot currently provide:- 

ozone density
nitrogen dioxide (NO 2)
sulphur_dioxide (E 220) density
PM25 density


The anlaogue output of the MQ135 is connected to the anlog input of you ESP module. If you are using a nodeMCU or Weemos D1 these have a built in voltage divider on the analogue input which scales 3.3v to 1v, so you need to connect a 180k resistor inline betwen the sensor and the anlogue input pin to allow this to scale From 5v to 1v. If you are connecting directly to a bare ESP8266 chip, then you need to add a voltage divider between the sensor and the analog input to scale form 5v to 1v.


The DHT22 temperature sensor is connected to GPIO5

There is also an LED connected to GPIO13. This should be connected to +3v though a suitable resistor for your led. 

![alt text](https://github.com/maccoylton/esp-homekit-air-quality-sensor/blob/master/Air%20Quality%20Sensor_bb.jpg)
