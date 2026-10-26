/* =========================================================
   BLE
   ========================================================= */

#define REMOTE_BLE_MAC "14:63:93:C5:CE:32"
#define REMOTE_BLE_NAME "ARTHRITIS_BAND"

#define SERVICE_UUID \
"12345678-1234-1234-1234-1234567890AB"

#define CHARACTERISTIC_UUID \
"87654321-4321-4321-4321-BA0987654321"

/* =========================================================
   BLE VARIABLES
   ========================================================= */

NimBLEClient *pClient = nullptr;
NimBLERemoteCharacteristic *pRemoteCharacteristic = nullptr;

#define RECONNECT_INTERVAL 500UL
unsigned long lastReconnectAttempt = 0;

/* =========================================================
   BLE
   ========================================================= */

void notifyCallback(
  NimBLERemoteCharacteristic *pCharacteristic,
  uint8_t *pData,
  size_t length,
  bool isNotify
)
{
  String command = "";
  for (size_t i = 0; i < length; i++)
  {
    command += (char)pData[i];
  }
  command.trim();

  if (
    command == "G1" || command == "G2" || command == "G3" ||
    command == "G4" || command == "G5" || command == "G6" ||
    command == "G7" || command == "G8" || command == "G9" ||
    command == "G10" ||
    (moreCommands &&
      (command == "G11" || command == "G12" || command == "G13"))
  )
  {
    receivedCommand = command;
    newCommandReceived = true;
  }
}

class ClientCallbacks : public NimBLEClientCallbacks
{
  void onConnect(NimBLEClient *pClient)
  {
    bleConnected = true;
    Serial.println("BLE connected");
    drawBLEStatus();
  }
  void onDisconnect(
    NimBLEClient *pClient,
    int reason
  )
  {
    bleConnected = false;
    pRemoteCharacteristic = nullptr;
    Serial.println("BLE disconnected");
    drawBLEStatus();
  }
};

/* =========================================================
   BLUETOOTH CHECK
   ========================================================= */

void checkBluetooth()
{
  if (bleConnected)
  {
    return;
  }
  if (
    millis() - lastReconnectAttempt >=
    RECONNECT_INTERVAL
  )
  {
    lastReconnectAttempt = millis();
    connectToBand();
  }
}

/* =========================================================
   CONNECT TO BAND
   =========================================================
   SAME CONNECTION METHOD AS YOUR WORKING CODE.
   ========================================================= */

bool connectToBand()
{
  Serial.println();
  Serial.println("Scanning for ARTHRITIS_BAND...");
  NimBLEScan *pScan =
    NimBLEDevice::getScan();
  pScan->clearResults();
  pScan->setActiveScan(true);
  pScan->setInterval(45);
  pScan->setWindow(40);
  NimBLEScanResults scanResults =
    pScan->getResults(1000);
  const NimBLEAdvertisedDevice *foundDevice =
    nullptr;

  // search by MAC
  for (
    int i = 0;
    i < scanResults.getCount();
    i++
  )
  {
    const NimBLEAdvertisedDevice *device =
      scanResults.getDevice(i);
    String address =
      device->getAddress().toString().c_str();
    address.toUpperCase();
    if (address == REMOTE_BLE_MAC)
    {
      foundDevice = device;
      Serial.println("Found by MAC");
      break;
    }
  }

  // search by name
  if (foundDevice == nullptr)
  {
    for (
      int i = 0;
      i < scanResults.getCount();
      i++
    )
    {
      const NimBLEAdvertisedDevice *device =
        scanResults.getDevice(i);
      if (device->haveName())
      {
        String name =
          device->getName().c_str();
        if (name == REMOTE_BLE_NAME)
        {
          foundDevice = device;
          Serial.println("Found by NAME");
          break;
        }
      }
    }
  }

  // band not found
  if (foundDevice == nullptr)
  {
    Serial.println("Band not found");
    drawBLEStatus();
    return false;
  }

  // create client
  if (pClient == nullptr)
  {
    pClient =
      NimBLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCallbacks(), false);
    pClient->setConnectionParams(12, 24, 0, 200);
    pClient->setConnectTimeout(10000);
  }

  // connect
  if (!pClient->connect(foundDevice))
  {
    Serial.println("BLE connection failed");
    bleConnected = false;
    return false;
  }

  // service
  NimBLERemoteService *pService =
    pClient->getService(SERVICE_UUID);
  if (pService == nullptr)
  {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  // characteristic
  pRemoteCharacteristic =
    pService->getCharacteristic(CHARACTERISTIC_UUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.println("Characteristic not found");
    pClient->disconnect();
    return false;
  }

  // subscribe
  if (
    pRemoteCharacteristic->canNotify()
  )
  {
    if (
      !pRemoteCharacteristic->subscribe(
        true,
        notifyCallback
      )
    )
    {
      Serial.println("Subscribe failed");
      pClient->disconnect();
      return false;
    }
  }
  else
  {
    Serial.println("Characteristic cannot notify");
    pClient->disconnect();
    return false;
  }

  // success
  bleConnected = true;
  drawBLEStatus();
  display.display();
  Serial.println("BLE connection successful");
  return true;
}
