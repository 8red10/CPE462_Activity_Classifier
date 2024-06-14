/*
  @file       cpe462_final003.ino
  
  @brief      TinyML run or walk activity classifier.
  @details    Adapted from magic_wand example of the Harvard_TinyMLx library.

  @credit     Harvard_TinyMLx

  @author     Jack Krammer
  @date       June 14, 2024
  @copyright  (c) by Jack Krammer and released under MIT license
*/

/* Include TensorFlow libraries */
#include <TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"
/* Include feature extraction functions */
#include "rasterize_stroke.h"
#include "imu_provider.h"

/* Include model data */
#include "run_walk_model_data.h"
/* Include UUID values for BLE */
#include "ble_uuid.h"

/* Namespace */
namespace {

  const int VERSION = 0x00000000;

  /* Constants for image rasterization */
  constexpr int raster_width = 32;
  constexpr int raster_height = 32;
  constexpr int raster_channels = 3;
  constexpr int raster_byte_count = raster_height * raster_width * raster_channels;
  int8_t raster_buffer[raster_byte_count];

  /* BLE settings */
  BLEService service(BLE_UUID_ACTIVITY_SERVICE);
  BLEByteCharacteristic labelCharacteristic(BLE_UUID_LABEL, BLERead | BLENotify);
  BLEByteCharacteristic scoreCharacteristic(BLE_UUID_SCORE, BLERead | BLENotify);

  /* Pins */
  const int BLE_LED_PIN = LED_BUILTIN;
  
  /* String to calculate the local and device name */
  String name;
  
  /* Create an area of memory to use for input, output, and intermediate arrays. 
  // The size of this will depend on the model you're using, and may need to be
  // determined by experimentation. */
  constexpr int kTensorArenaSize = 30 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
  
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  
  /* Label info to match with dataset */
  /* "1" = turn left while stationary (standing in place) */
  /* "2" = walk                                           */
  /* "3" = turn right                                     */
  /* "4" = running                                        */
  constexpr int label_count = 4;
  const char* labels[label_count] = {"1", "2", "3", "4"};

}  // namespace



void setup() {
  /* Start serial */
  Serial.begin(9600);
  /* Hang here until Serial is configured */   
  while(!Serial); /* ------------------------------ COMMENT OUT THIS LINE IF POWERING THE BOARD WITH AN EXTERNAL BATTERY ------------------------------ */
  /* Indicate serial started */
  Serial.println("Started.");

  /* Set pin to output */
  pinMode(BLE_LED_PIN, OUTPUT);
  digitalWrite(BLE_LED_PIN, LOW);

  /* Try to start IMU */
  if (!IMU.begin()) 
  {
    /* Indicate failure */
    Serial.println("IMU initialization failed.");
    while(1);
  }
  /* Configure IMU settings */ 
  SetupIMU();
  /* Indicate success */
  Serial.println("IMU configuration success.");

  /* Try to start BLE */
  if(!BLE_Config()) 
  {
    /* Indicate failure */
    Serial.println("BLE configuration failed.");
    while(1);
  }
  /* Inidcate success */
  Serial.println("BLE configuration success.");
  digitalWrite(BLE_LED_PIN, HIGH);
  
  // Set up logging. Google style is to avoid globals or statics because of
  // lifetime uncertainty, but since this has a trivial destructor it's okay.
  static tflite::MicroErrorReporter micro_error_reporter;  // NOLINT
  error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_run_walk_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroMutableOpResolver<4> micro_op_resolver;  // NOLINT
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddMean();
  micro_op_resolver.AddFullyConnected();
  micro_op_resolver.AddSoftmax();

  // Build an interpreter to run the model with.
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  interpreter->AllocateTensors();

  // Set model input settings
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != raster_height) ||
      (model_input->dims->data[2] != raster_width) ||
      (model_input->dims->data[3] != raster_channels) ||
      (model_input->type != kTfLiteInt8)) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Bad input tensor parameters in model");
    return;
  }

  // Set model output settings
  TfLiteTensor* model_output = interpreter->output(0);
  if ((model_output->dims->size != 2) || (model_output->dims->data[0] != 1) ||
      (model_output->dims->data[1] != label_count) ||
      (model_output->type != kTfLiteInt8)) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Bad output tensor parameters in model");
    return;
  }

}



void loop() {
  /* Listen for BLE devices to connect to */
  BLEDevice central = BLE.central();
  
  // if a central is connected to the peripheral:
  static bool was_connected_last = false;  
  if (central && !was_connected_last) {
    Serial.print("Connected to central: ");
    // print the central's BT address:
    Serial.println(central.address());
  }
  was_connected_last = central;

  // make sure IMU data is available then read in data
  const bool data_available = IMU.accelerationAvailable() || IMU.gyroscopeAvailable();
  if (!data_available) {
    return;
  }
  int accelerometer_samples_read;
  int gyroscope_samples_read;
  ReadAccelerometerAndGyroscope(&accelerometer_samples_read, &gyroscope_samples_read);

  // Parse and process IMU data
  bool done_just_triggered = false;
  if (gyroscope_samples_read > 0) {
    EstimateGyroscopeDrift(current_gyroscope_drift);
    UpdateOrientation(gyroscope_samples_read, current_gravity, current_gyroscope_drift);
    UpdateStroke(gyroscope_samples_read, &done_just_triggered);
    // if (central && central.connected()) {
    //   strokeCharacteristic.writeValue(stroke_struct_buffer, stroke_struct_byte_count);
    // }
  }
  if (accelerometer_samples_read > 0) {
    EstimateGravityDirection(current_gravity);
    UpdateVelocity(accelerometer_samples_read, current_gravity);
  }

  // Wait for a gesture to be done
  if (done_just_triggered) {
    // Rasterize the gesture
    RasterizeStroke(stroke_points, *stroke_transmit_length, 0.6f, 0.6f, raster_width, raster_height, raster_buffer);
    for (int y = 0; y < raster_height; ++y) {
      char line[raster_width + 1];
      for (int x = 0; x < raster_width; ++x) {
        const int8_t* pixel = &raster_buffer[(y * raster_width * raster_channels) + (x * raster_channels)];
        const int8_t red = pixel[0];
        const int8_t green = pixel[1];
        const int8_t blue = pixel[2];
        char output;
        if ((red > -128) || (green > -128) || (blue > -128)) {
          output = '#';
        } else {
          output = '.';
        }
        line[x] = output;
      }
      line[raster_width] = 0;
      /* Print the current stroke over serial */
      Serial.println(line);
    }

    // Pass to the model and run the interpreter
    TfLiteTensor* model_input = interpreter->input(0);
    for (int i = 0; i < raster_byte_count; ++i) {
      model_input->data.int8[i] = raster_buffer[i];
    }
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed");
      return;
    }
    TfLiteTensor* output = interpreter->output(0);

    // Parse the model output
    int8_t max_score;
    int max_index;
    for (int i = 0; i < label_count; ++i) {
      const int8_t score = output->data.int8[i];
      if ((i == 0) || (score > max_score)) {
        max_score = score;
        max_index = i;
      }
    }
    /* Prints the output category */ 
    TF_LITE_REPORT_ERROR(error_reporter, "Found %s (%d)", labels[max_index], max_score);
    /* Check if there is a central (device) connected */
    if(central)
    {
      /* Convert label to a char to send over BLE */
      unsigned char charLabel = labels[max_index][0];
      /* Send the output label over BLE */
      labelCharacteristic.writeValue(charLabel);
      /* Send the output score over BLE */
      scoreCharacteristic.writeValue(max_score);
    } 
  }
}



bool BLE_Config()
{
  /* Starting BLE configuration */
  Serial.println("Starting BLE configuration.");
  /* Try to start BLE */
  if(!BLE.begin())
  {
    /* Indicate BLE failed */
    return false;
  }

  /* Get the MAC address of this device */
  String address = BLE.address();

  /* Display MAC address */
  Serial.print("BLE address = ");
  Serial.println(address);

  /* Create name for BLE connectivity */
  address.toUpperCase();
  name = "BLESense-";
  name += address[address.length() - 5];
  name += address[address.length() - 4];
  name += address[address.length() - 2];
  name += address[address.length() - 1];
  /* Display advertised BLE name */
  Serial.print("BLE name (RECORD THIS NAME) = ");
  Serial.println(name);
  /* Set the name */ 
  BLE.setLocalName(name.c_str());
  BLE.setDeviceName(name.c_str());
  BLE.setAdvertisedService(service);

  /* Add characteristics to service */ 
  // service.addCharacteristic(strokeCharacteristic);
  service.addCharacteristic(labelCharacteristic);
  service.addCharacteristic(scoreCharacteristic);

  /* Add service to BLE */
  BLE.addService(service);
  /* Advertise service over BLE */
  BLE.advertise();

  /* Indicate success */
  return true;
}

