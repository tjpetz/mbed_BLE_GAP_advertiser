/* mbed Microcontroller Library
 * Copyright (c) 2006-2018 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 /**
  * Updating to support advertising on both legacy and extended (using coded phy). 
  */

/**
 * TJP: Contrary to the documentation, it does not appear you can set 2 advertising sets
 * right after each other.  Rather you need to do this via dispatch, adding only
 * 1 advertising set at at time.
 */
 
#include <mbed.h>
#include <events/mbed_events.h>
#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ble/gap/AdvertisingDataParser.h"

using namespace std::chrono;
using std::milli;
using namespace std::literals::chrono_literals;

static mbed::DigitalOut led1(LED1, 0);

// Redirect the console to the USBSerial port
REDIRECT_STDOUT_TO(Serial);

// Advertiser

static const ble::AdvertisingParameters advertising_params(
    ble::advertising_type_t::CONNECTABLE_UNDIRECTED,
    ble::adv_interval_t(ble::millisecond_t(80)), /* this could also be expressed as ble::adv_interval_t(40) */
    ble::adv_interval_t(ble::millisecond_t(160)) /* this could also be expressed as ble::adv_interval_t(80) */
);

static const ble::AdvertisingParameters advertising_params2(
    ble::advertising_type_t::CONNECTABLE_UNDIRECTED,
    ble::adv_interval_t(ble::millisecond_t(90)), /* this could also be expressed as ble::adv_interval_t(40) */
    ble::adv_interval_t(ble::millisecond_t(180)) /* this could also be expressed as ble::adv_interval_t(80) */
);

/* if the controller support it we can advertise multiple sets */
static const ble::AdvertisingParameters extended_advertising_params(
    ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED,
    ble::adv_interval_t(600),
    ble::adv_interval_t(800),
    false
);

static const std::chrono::milliseconds advertising_duration = 10000ms;

/* config end */

events::EventQueue event_queue;


inline void print_error(ble_error_t err, const char* msg) {
  printf("%s: %s", msg, BLE::errorToString(err));
}

/** print device address to the terminal */
inline void print_address(const ble::address_t &addr)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x\r\n",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

inline void print_mac_address()
{
    /* Print out device MAC address to the console*/
    ble::own_address_type_t addr_type;
    ble::address_t address;
    BLE::Instance().gap().getAddress(addr_type, address);
    printf("DEVICE MAC ADDRESS: ");
    print_address(address);
}

inline const char* phy_to_string(ble::phy_t phy) {
    switch(phy.value()) {
        case ble::phy_t::LE_1M:
            return "LE 1M";
        case ble::phy_t::LE_2M:
            return "LE 2M";
        case ble::phy_t::LE_CODED:
            return "LE coded";
        default:
            return "invalid PHY";
    }
}


/** Demonstrate advertising, scanning and connecting */
class GapDemo : private mbed::NonCopyable<GapDemo>, public ble::Gap::EventHandler
{
public:
    GapDemo(BLE& ble, events::EventQueue& event_queue) :
        _ble(ble),
        _gap(ble.gap()),
        _event_queue(event_queue)
    {
    }

    ~GapDemo()
    {
        if (_ble.hasInitialized()) {
            _ble.shutdown();
        }
    }

    /** Start BLE interface initialisation */
    void run()
    {
        /* handle gap events */
        _gap.setEventHandler(this);

        ble_error_t error = _ble.init(this, &GapDemo::on_init_complete);
        if (error) {
            print_error(error, "Error returned by BLE::init");
            return;
        }

        /* this will not return until shutdown */
        _event_queue.dispatch_forever();
    }

private:
    /** This is called when BLE interface is initialised and starts the first mode */
    void on_init_complete(BLE::InitializationCompleteCallbackContext *event)
    {
        if (event->error) {
            print_error(event->error, "Error during the initialisation");
            return;
        }

        print_mac_address();

        /* setup the default phy used in connection to 2M to reduce power consumption */
        if (_gap.isFeatureSupported(ble::controller_supported_features_t::LE_2M_PHY)) {
            ble::phy_set_t phys(/* 1M */ false, /* 2M */ true, /* coded */ false);

            ble_error_t error = _gap.setPreferredPhys(/* tx */&phys, /* rx */&phys);

            /* PHY 2M communication will only take place if both peers support it */
            if (error) {
                print_error(error, "GAP::setPreferedPhys failed");
            } else {
              printf("Upgrade PHY to 2M\n");
            }


        } else {
            /* otherwise it will use 1M by default */
        }

        /* all calls are serialised on the user thread through the event queue */
        _event_queue.call(this, &GapDemo::advertise);
    }

    /** Set up and start advertising */
    void advertise()
    {
        ble_error_t error;

        printf("Max advertising sets = %d\n", _gap.getMaxAdvertisingSetNumber());
        
        // error = _gap.setAdvertisingParameters(ble::LEGACY_ADVERTISING_HANDLE, 
        //   advertising_params
        //   );
        // if (error) {
        //     print_error(error, "Gap::setAdvertisingParameters() failed");
        //     return;
        // }

        // // Add the TX power to the header.
        // error = _gap.setAdvertisingParameters(ble::LEGACY_ADVERTISING_HANDLE,
        //   ble::AdvertisingParameters()
        //     .setOwnAddressType(ble::own_address_type_t::PUBLIC)            
        //     .setTxPower(4)
        //     .includeTxPowerInHeader(true));
        // if (error) {
        //   print_error(error, "Gap::setAdvertisingParamters() update failed");
        // }

        /* to create a payload we'll use a helper class that builds a valid payload */
        /* AdvertisingDataSimpleBuilder is a wrapper over AdvertisingDataBuilder that allocated the buffer for us */
        ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE> data_builder;

        // /* builder methods can be chained together as they return the builder object */
        // data_builder.setFlags().setName("Legacy Set").setTxPowerAdvertised(4);

        // /* Set payload for the set */
        // error = _gap.setAdvertisingPayload(ble::LEGACY_ADVERTISING_HANDLE, data_builder.getAdvertisingData());
        // if (error) {
        //     print_error(error, "Gap::setAdvertisingPayload() failed");
        //     return;
        // }

        /* Start advertising the set */
        // error = _gap.startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);
        // if (error) {
        //     print_error(error, "Gap::startAdvertising() failed");
        //     return;
        // }

        // printf(
        //     "\r\nAdvertising started (type: 0x%x, interval: [%d : %d]ms)\r\n",
        //     advertising_params.getType(),
        //     advertising_params.getMinPrimaryInterval().valueInMs(),
        //     advertising_params.getMaxPrimaryInterval().valueInMs()
        // );

        // Setup a primary advertising set.
        // error = _gap.createAdvertisingSet(&_primary_adv_handle, advertising_params);
        // if (!error) {
        //   error = _gap.setAdvertisingParameters(
        //     _primary_adv_handle,
        //     ble::AdvertisingParameters()
        //       .setType(ble::advertising_type_t::CONNECTABLE_UNDIRECTED, true)
        //       .setOwnAddressType(ble::own_address_type_t::PUBLIC)            
        //       .setTxPower(4)
        //       .includeTxPowerInHeader(true)
        //       );
        //   if (!error) {
        //     data_builder.setName("Primary Set").setTxPowerAdvertised(4);

        //     error = _gap.setAdvertisingPayload(_primary_adv_handle, data_builder.getAdvertisingData());
        //     if (!error) {
        //       error = _gap.startAdvertising(_primary_adv_handle);
        //       if (!error) {
        //         printf(
        //             "\r\nAdvertising started (type: 0x%x, handle: %d, interval: [%d : %d]ms)\r\n",
        //             advertising_params.getType(),
        //             _primary_adv_handle,
        //             advertising_params.getMinPrimaryInterval().valueInMs(),
        //             advertising_params.getMaxPrimaryInterval().valueInMs()
        //         );
        //       } else {
        //         print_error(error, "Advertising the primary Advertising Set");
        //       }

        //     } else {
        //       print_error(error, "Setting primary Advertising Payload");
        //     }
        //   } else {
        //     print_error(error, "Setting Advertising Parameters for the primary set");
        //   }
                  
        // } else {
        //   print_error(error, "Creating primary advertising set");
        // }



#if BLE_FEATURE_EXTENDED_ADVERTISING
        /* if we support extended advertising we'll also additionally advertise another set at the same time */
        if (_gap.isFeatureSupported(ble::controller_supported_features_t::LE_EXTENDED_ADVERTISING)) {
            /* With Bluetooth 5; it is possible to advertise concurrently multiple
             * payloads at different rate. The combination of payload and its associated
             * parameters is named an advertising set. To refer to these advertising
             * sets the Bluetooth system use an advertising set handle that needs to
             * be created first.
             * The only exception is the legacy advertising handle which is usable
             * on Bluetooth 4 and Bluetooth 5 system. It is created at startup and
             * its lifecycle is managed by the system.
             */
            ble_error_t error = _gap.createAdvertisingSet(&_extended_adv_handle, extended_advertising_params);
            if (error) {
                print_error(error, "Gap::createAdvertisingSet() failed");
                return;
            }

            error = _gap.setAdvertisingParameters(
              _extended_adv_handle,
              ble::AdvertisingParameters()
                .setType(ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED, false)
                // .setPhy(ble::phy_t::LE_1M, ble::phy_t::LE_1M)
                .setPhy(ble::phy_t::LE_CODED, ble::phy_t::LE_CODED)
                .setTxPower(8)
                .includeTxPowerInHeader(true)
                .setScanRequestNotification(true)
            );

            if (error) {
              print_error(error, "Gap::setAdvertisingParameters");
            }            

            /* we can reuse the builder, we just replace the name */
            data_builder.setName("Extended Set").setTxPowerAdvertised(8);

            /* Set payload for the set */
            error = _gap.setAdvertisingPayload(_extended_adv_handle, data_builder.getAdvertisingData());
            if (error) {
                print_error(error, "Gap::setAdvertisingPayload() failed");
                return;
            }

            /* Start advertising the set */
            error = _gap.startAdvertising(_extended_adv_handle);
            if (error) {
                print_error(error, "Gap::startAdvertising() failed");
                return;
            }

            printf(
                "Advertising started (type: 0x%x, interval: [%d : %d]ms)\r\n",
                extended_advertising_params.getType(),
                extended_advertising_params.getMinPrimaryInterval().valueInMs(),
                extended_advertising_params.getMaxPrimaryInterval().valueInMs()
              );
        }
#endif // BLE_FEATURE_EXTENDED_ADVERTISING

      // _ble.signalEventsToProcess();
      // Add2ndAdvertisingSet();
      event_queue.call_in(3000ms, this, &GapDemo::Add2ndAdvertisingSet);

    }

    void Add2ndAdvertisingSet() 
    {
      ble_error_t error;
      ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE> data_builder;

      // Setup an additional advertising set.
      error = _gap.createAdvertisingSet(&_second_adv_handle, advertising_params2);
      if (!error) {
        error = _gap.setAdvertisingParameters(
          _second_adv_handle,
          ble::AdvertisingParameters()
            .setType(ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED, true)
            .setOwnAddressType(ble::own_address_type_t::PUBLIC)            
            .setTxPower(6)
            .includeTxPowerInHeader(true)
            .setScanRequestNotification(true)
            );
        if (!error) {
          data_builder.setName("2nd Set").setTxPowerAdvertised(6);

          error = _gap.setAdvertisingPayload(_second_adv_handle, data_builder.getAdvertisingData());
          if (!error) {
            error = _gap.startAdvertising(_second_adv_handle);
            if (!error) {
              printf(
                  "\r\nAdvertising started (type: 0x%x, handle: %d, interval: [%d : %d]ms)\r\n",
                  advertising_params2.getType(),
                  _second_adv_handle,
                  advertising_params2.getMinPrimaryInterval().valueInMs(),
                  advertising_params2.getMaxPrimaryInterval().valueInMs()
              );
            } else {
              print_error(error, "Advertising the 2nd Advertising Set");
            }

          } else {
            print_error(error, "Setting 2nd Advertising Payload");
          }
        } else {
          print_error(error, "Setting Advertising Parameters for the 2nd set");
        }
                
      } else {
        print_error(error, "Creating second advertising set");
      }

    }    
private:
    /* Gap::EventHandler */

    void onScanRequestReceived(const ble::ScanRequestEvent &event) override
    {
      printf("Scan request received from: ");
      print_address(event.getPeerAddress());
    }    

    void onAdvertisingStart(const ble::AdvertisingStartEvent &advHandle) override
    {
      printf("Advertising started on handle: %d\n", advHandle);
    }

    void onAdvertisingEnd(const ble::AdvertisingEndEvent &event) override
    {
        if (event.isConnected()) {
            Serial.print("Stopped advertising early due to connection\r\n");
        }
    }

    void onConnectionComplete(const ble::ConnectionCompleteEvent &a) override {
      printf("Connection from: ");
      print_address(a.getPeerAddress());
      led1 = 1;
    }

    /** This is called by Gap to notify the application we disconnected,
     *  in our case it calls next_demo_mode() to progress the demo */
    void onDisconnectionComplete(const ble::DisconnectionCompleteEvent &event) override
    {
        led1 = 0;
        printf("Disconnected\r\n");

        // Restart advertising
        ble_error_t error = _gap.startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);
        if (error) {
            print_error(error, "Gap::startAdvertising() failed");
            return;
        }
#if BLE_FEATURE_EXTENDED_ADVERTISING
        error = _gap.startAdvertising(_extended_adv_handle);
        if (error) {
            print_error(error, "Gap::startAdvertising() failed");
            return;
        }
#endif

    }

    /**
     * Implementation of Gap::EventHandler::onReadPhy
     */
    void onReadPhy(
        ble_error_t error,
        ble::connection_handle_t connectionHandle,
        ble::phy_t txPhy,
        ble::phy_t rxPhy
      ) override
    {
        if (error) {
            printf(
                "Phy read on connection %d failed with error code %s\r\n",
                connectionHandle, BLE::errorToString(error)
            ); 
        } else {
            printf(
                "Phy read on connection %d - Tx Phy: %s, Rx Phy: %s\r\n",
                connectionHandle, phy_to_string(txPhy), phy_to_string(rxPhy)
            );
        }
    }

    /**
     * Implementation of Gap::EventHandler::onPhyUpdateComplete
     */
    void onPhyUpdateComplete(
        ble_error_t error,
        ble::connection_handle_t connectionHandle,
        ble::phy_t txPhy,
        ble::phy_t rxPhy
    ) override
    {
        if (error) {
            printf(
                "Phy update on connection: %d failed with error code %s\r\n",
                connectionHandle, BLE::errorToString(error)
            ); 
        } else {
            printf(
                "Phy update on connection %d - Tx Phy: %s, Rx Phy: %s\r\n",
                connectionHandle, phy_to_string(txPhy), phy_to_string(rxPhy)
            ); 
        }
    }

    /**
     * Implementation of Gap::EventHandler::onDataLengthChange
     */
    void onDataLengthChange(
        ble::connection_handle_t connectionHandle,
        uint16_t txSize,
        uint16_t rxSize
    ) override
    {
        printf(
            "Data length changed on the connection %d.\r\n"
            "Maximum sizes for over the air packets are:\r\n"
            "%d octets for transmit and %d octets for receive.\r\n",
            connectionHandle, txSize, rxSize
        ); 
    }

private:


private:
    BLE &_ble;
    ble::Gap &_gap;
    events::EventQueue &_event_queue;
    ble::advertising_handle_t _primary_adv_handle = ble::INVALID_ADVERTISING_HANDLE;
    ble::advertising_handle_t _second_adv_handle = ble::INVALID_ADVERTISING_HANDLE;

#if BLE_FEATURE_EXTENDED_ADVERTISING
    ble::advertising_handle_t _extended_adv_handle = ble::INVALID_ADVERTISING_HANDLE;
#endif // BLE_FEATURE_EXTENDED_ADVERTISING
};

/** Schedule processing of events from the BLE middleware in the event queue. */
void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context)
{
    event_queue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}

void setup()
{
    BLE &ble = BLE::Instance();

    delay(7000);
    
    /* this will inform us off all events so we can schedule their handling
     * using our event queue */
    ble.onEventsToProcess(schedule_ble_events);

    GapDemo demo(ble, event_queue);

    demo.run();
}

void loop() {
  // put your main code here, to run repeatedly:

}
