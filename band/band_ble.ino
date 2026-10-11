// =====================================================
// BLE SETTINGS
// =====================================================

#define BLUETOOTH_NAME "ARTHRITIS_BAND"

#define SERVICE_UUID \
"12345678-1234-1234-1234-1234567890AB"

#define CHARACTERISTIC_UUID \
"87654321-4321-4321-4321-BA0987654321"

// =====================================================
// BLE SERVER CALLBACKS
// =====================================================

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;

    Serial.println();
    Serial.println("======================================");
    Serial.println(" BLE CONNECTED");
    Serial.println("======================================");
  }

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    Serial.println();
    Serial.println("BLE DISCONNECTED");
    Serial.println("Restarting advertising...");
    delay(100);
    pServer->startAdvertising();
  }
};

// =====================================================
// START BLE
// =====================================================

void startBle()
{
  // BLE
  BLEDevice::init(BLUETOOTH_NAME);
  // BLE MAC
  Serial.print("Actual BLE MAC: ");
  Serial.println(BLEDevice::getAddress().toString().c_str());
  // BLE server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // BLE characteristic
  gestureCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  // notifications
  gestureCharacteristic->addDescriptor(new BLE2902());
  // start service
  pService->start();
  // BLE advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  // startup information
  Serial.println();
  Serial.println("======================================");
  Serial.println(" ARTHRITIS WRIST BAND");
  Serial.println(" BLE TRANSMITTER");
  Serial.println("======================================");

  Serial.print("BLE Name : ");
  Serial.println(BLUETOOTH_NAME);
  Serial.print("Service UUID : ");
  Serial.println(SERVICE_UUID);
  Serial.print("Characteristic UUID : ");
  Serial.println(CHARACTERISTIC_UUID);
  Serial.println();
  Serial.println("Waiting for BLE connection...");
}
