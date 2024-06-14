/*
  @file       ble_uuid.h
  @brief      A header file to store the UUID.
  @details    For use with the run walk activity classifier.
  @author     Jack Krammer
  @date       June 14, 2024
  @copyright  (c) 2024 by Jack Krammer and released under MIT license
*/

#ifndef BLE_UUID_H_
#define BLE_UUID_H_

/* ------------------------------------------------------------ */
/*                                                              */
/*             CHANGE THE UUID VALUES DEFINED BELOW             */
/*                                                              */
/* ------------------------------------------------------------ */

#define BLE_UUID_ACTIVITY_SERVICE         "a2e2c2a6-08c3-4d68-b80a-1fab55ba1dd0" /* CHANGE this to a different generated UUID */
#define BLE_UUID_LABEL                    "0000" /* CHANGE this to a different random 16 bit (2 byte) value */
#define BLE_UUID_SCORE                    "aaaa" /* CHANGE this to a different random 16 bit (2 byte) value */ 

#endif  // BLE_UUID_H_


