# mbed_BLE_GAP_advertiser

[![CI](https://github.com/tjpetz/mbed_BLE_GAP_advertiser/actions/workflows/build.yml/badge.svg)](https://github.com/tjpetz/mbed_BLE_GAP_advertiser/actions/workflows/build.yml)

This example is based on the mbed OS GAP advertiser example.

It only works with the Arduino BLE 33 and Arduino BLE 33 Sense.  (It may work with the Portana but
this is not tested.)  It is an example of using the MbedOS BLE library on Arduino.

In this example we demonstrate advertising 2 advertising sets.  The first set is advertised on the 
legacy advertising set on the 1M PHY.  The second advertising set is advertised on the CODED PHY.
