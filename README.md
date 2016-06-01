## ESP8266 skech for Arduino IDE that sends temperature & humidity to OVH IoT PaaS (OpenTSDB) and/or ThingSpeak.com

#Usage

* Set your SSID and associated WIFI password

* Depending on the IoT service(s) you want to use, set the required credentials as specified in the code comments.

* You may enable the OVH IoT PaaS (OpenTDSB) or ThingSpeak connections by defining the constants ENABLE_OVH_IOT_PAAS and ENABLE_THINGSPEAK

* The default LOOP_DELAY is 300 000 ms (5min) between each measure

* The DHT22 sensor is connected on GPIO 2 as specified by the DHTPIN constant.
* 
* The metrics are sent with the names *temperature.work* and *humidity.work*. The ".work" sub-metric is because I use 2 of these things. One at home with the submetric ".home" and one at work.
