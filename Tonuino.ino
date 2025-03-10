#include <DFMiniMp3.h>
#include <EEPROM.h>
#include <JC_Button.h>
#include <MFRC522.h>
#include <SPI.h>
#include <SoftwareSerial.h>

// DFPlayer Mini
SoftwareSerial mySoftwareSerial(2, 3); // RX, TX
uint16_t numTracksInFolder;
uint16_t currentTrack;

/* Object to save data read from the NFC card
 */
struct nfcTagObject {
  uint32_t cookie;
  uint8_t version;
  uint8_t folder;
  uint8_t mode;
  uint8_t special;
};

nfcTagObject myCard;

static void nextTrack(uint16_t track);
int voiceMenu(
  int numberOfOptions,
  int startMessage,
  int messageOffset,
  bool preview = false,
  int previewFromFolder = 0
);

bool knownCard = false;

/* DfMp3 Notification class
 *
 * The member functions are called for their respective events
 */
class Mp3Notify {
public:
  static void OnError(uint16_t errorCode) {
    // see DfMp3_Error for code meaning
    Serial.println();
    Serial.print(F("COM Error: "));
    Serial.println(errorCode);
  }

  static void OnPlayFinished(uint16_t track) {
    Serial.print(F("Track beendet: "));
    Serial.println(track);
    delay(100);
    nextTrack(track);
  }

  static void OnCardOnline(uint16_t code) {
    Serial.println(F("SD Karte online "));
  }

  static void OnCardInserted(uint16_t code) {
    Serial.println(F("SD Karte bereit "));
  }

  static void OnCardRemoved(uint16_t code) {
    Serial.println(F("SD Karte entfernt "));
  }

  static void OnUsbOnline(uint16_t code) {
      Serial.println(F("USB online "));
  }

  static void OnUsbInserted(uint16_t code) {
      Serial.println(F("USB bereit "));
  }

  static void OnUsbRemoved(uint16_t code) {
    Serial.println(F("USB entfernt "));
  }
};

static DFMiniMp3<SoftwareSerial, Mp3Notify> mp3(mySoftwareSerial);
static uint16_t _lastTrackFinished;


/* Go to next track, according to playback mode
 *
 * Triggered by button presses or the previous track ending
 */
static void nextTrack(uint16_t track) {
    // Ignore nextTrack() calls during card learning
  if (!knownCard) return;

  // Check and save last finished track. Ignore nextTrack() calls
  // if the last track of a folder is already playing
  if (track == _lastTrackFinished) return;
  _lastTrackFinished = track;

  // Determine next track based on playback mode
  switch (myCard.mode) {
    // Audio drama mode
    case 1:
      Serial.println(F("Audio drama mode: Not skipping track."));
      break;
    // Album mode
    case 2:
      if (currentTrack != numTracksInFolder) {
        currentTrack++;
        mp3.playFolderTrack(myCard.folder, currentTrack);

        Serial.print(F("Album mode: Playing track "));
        Serial.println(currentTrack);
      }
      break;
    // Party mode
    case 3: {
        // Randomize new track
        uint16_t oldTrack = currentTrack;
        currentTrack = random(1, numTracksInFolder + 1);

        // If a song would be played twice, play next track instead
        if (currentTrack == oldTrack) currentTrack++;
        // If song to played does not exist, play first track instead
        if (currentTrack >= numTracksInFolder) currentTrack = 1;

        mp3.playFolderTrack(myCard.folder, currentTrack);

        Serial.print(F("Party mode: Playing track "));
        Serial.println(currentTrack);
      }
      break;
    // Single track mode
    case 4:
      Serial.println(F("Single track mode: Not skipping track"));
      break;
    // Audio book mode
    case 5:
      if (currentTrack != numTracksInFolder) {
        currentTrack++;
        mp3.playFolderTrack(myCard.folder, currentTrack);
        // Save progress to EEPROM
        EEPROM.write(myCard.folder, currentTrack);

        Serial.print(F("Audio book mode: Playing and saving track: "));
        Serial.println(currentTrack);
      } else {
        // End of audio book, reset progress in EEPROM
        EEPROM.write(myCard.folder, 1);
        Serial.print(F("Audio book mode: End reached"));
      }
      break;
    // Unknown mode
    default:
      Serial.print(F("Unknown card mode: "));
      Serial.println(myCard.mode);
  }
}

/* Go to previous track, according to playback mode
 *
 * Triggered by button presses
 */
static void previousTrack() {
  switch (myCard.mode) {
    // Audio drama mode
    case 1:
      mp3.playFolderTrack(myCard.folder, currentTrack);
      Serial.println(F("Audio book mode: Restarting current track"));
      break;
    // Album mode
    case 2:
      if (currentTrack != 1) currentTrack--;
      mp3.playFolderTrack(myCard.folder, currentTrack);

      Serial.print(F("Album mode: Playing previous track: "));
      Serial.println(currentTrack);
      break;
    // Party mode
    case 3:
      mp3.playFolderTrack(myCard.folder, currentTrack);
      Serial.println(F("Party book mode: Restarting current track"));
      break;
    // Single track mode
    case 4:
      mp3.playFolderTrack(myCard.folder, currentTrack);
      Serial.println(F("Single track mode: Restarting current track"));
      break;
    // Audio book mode
    case 5:
      if (currentTrack != 1)  currentTrack--;
      mp3.playFolderTrack(myCard.folder, currentTrack);
      // Save progress in EEPROM
      EEPROM.write(myCard.folder, currentTrack);

      Serial.print(F("Audio book mode: Playing and saving track: "));
      Serial.println(currentTrack);
      break;
    // Unknown mode
    default:
      Serial.print(F("Unknown card mode: "));
      Serial.println(myCard.mode);
  }
}

// MFRC522
#define RST_PIN 9                 // Configurable, see typical pin layout above
#define SS_PIN 10                 // Configurable, see typical pin layout above
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522
MFRC522::MIFARE_Key key;
bool successRead;
byte sector = 1;
byte blockAddr = 4;
byte trailerBlock = 7;
MFRC522::StatusCode status;

#define BUTTON_PAUSE A0
#define BUTTON_UP A1
#define BUTTON_DOWN A2
#define BUSY_PIN 4
#define LONG_PRESS 1000
#define REPEAT_ACTION 300

Button pauseButton(BUTTON_PAUSE);
Button upButton(BUTTON_UP);
Button downButton(BUTTON_DOWN);
bool ignorePauseButton = false;
bool ignoreUpButton = false;
bool ignoreDownButton = false;

uint16_t upStep = LONG_PRESS;
uint16_t downStep = LONG_PRESS;

uint8_t numberOfCards = 0;

/* Retrieve DFPlayer playback status
 *
 * @return boolean True if a track is playing, false otherwise
 */
bool isPlaying() {
  return !digitalRead(BUSY_PIN);
}


/* Arduino setup function
 *
 * Automatically called on power on
 */
void setup() {
  // Initialize serial connection for debug output
  Serial.begin(115200);
  // Seed random generator for random track function
  randomSeed(analogRead(A0));

  // Print banner on serial connection
  Serial.println(F("TonUINO Version 2.0"));
  Serial.println(F("(c) Thorsten Voß"));

  // Set buttons to use PULLUP
  pinMode(BUTTON_PAUSE, INPUT_PULLUP);
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);

  // Create pin to get busy status from DFPlayer
  pinMode(BUSY_PIN, INPUT);

  // Initialize DFPlayer
  mp3.begin();
  mp3.setVolume(10);

  // NFC Leser initialisieren
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  mfrc522.PCD_DumpVersionToSerial(); // Show details of PCD - MFRC522 Card Reader
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }

  // Hold all three buttons to reset EEPROM
  if (
    digitalRead(BUTTON_PAUSE) == LOW
    && digitalRead(BUTTON_UP) == LOW
    && digitalRead(BUTTON_DOWN) == LOW
  ) {
    Serial.println(F("Reset: Clearing EEPROM"));
    for (int i = 0; i < EEPROM.length(); i++) {
      EEPROM.write(i, 0);
    }
  }

}

/* Arduino main loop
 *
 * Called once setup() has finished
 */
void loop() {
  do {
    // Start DFPlayer
    mp3.loop();

    // Pass button handling to JC library
    pauseButton.read();
    upButton.read();
    downButton.read();

    if (pauseButton.wasReleased()) {
      if (ignorePauseButton == false) {
        if (isPlaying())
          mp3.pause();
        else
          mp3.start();
      }
      ignorePauseButton = false;
    }
    else if (
      pauseButton.pressedFor(LONG_PRESS) &&
      ignorePauseButton == false
    ) {
      if (isPlaying())
        mp3.playAdvertisement(currentTrack);
      else {
        knownCard = false;
        mp3.playMp3FolderTrack(800);
        Serial.println(F("Resetting card ..."));
        resetCard();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
      ignorePauseButton = true;
    }

    if (upButton.pressedFor(upStep)) {
      Serial.println(F("Volume up"));
      mp3.increaseVolume();
      // Delay next volume step
      upStep += REPEAT_ACTION;
      // Don't play next track if volume has been adjusted
      ignoreUpButton = true;
    }
    else if (upButton.wasReleased()) {
      if (!ignoreUpButton) {
        nextTrack(mp3.getCurrentTrack());
      }
      else {
        ignoreUpButton = false;
        upStep = LONG_PRESS;
      }
    }

    if (downButton.pressedFor(downStep)) {
      Serial.println(F("Volume down"));
      mp3.decreaseVolume();
      // Delay next volume step
      downStep += REPEAT_ACTION;
      // Don't play previous track if volume has been adjusted
      ignoreDownButton = true;
    }
    else if (downButton.wasReleased()) {
      if (!ignoreDownButton){
        previousTrack();
      }
      else {
        ignoreDownButton = false;
        downStep = LONG_PRESS;
      }
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  // RFID Karte wurde aufgelegt

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  if (readCard(&myCard) == true) {
    if (myCard.cookie == 322417479 && myCard.folder != 0 && myCard.mode != 0) {

      knownCard = true;
      _lastTrackFinished = 0;
      numTracksInFolder = mp3.getFolderTrackCount(myCard.folder);
      Serial.print(numTracksInFolder);
      Serial.print(F(" Dateien in Ordner "));
      Serial.println(myCard.folder);

      // Hörspielmodus: eine zufällige Datei aus dem Ordner
      if (myCard.mode == 1) {
        Serial.println(F("Hörspielmodus -> zufälligen Track wiedergeben"));
        currentTrack = random(1, numTracksInFolder + 1);
        Serial.println(currentTrack);
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      // Album Modus: kompletten Ordner spielen
      if (myCard.mode == 2) {
        Serial.println(F("Album Modus -> kompletten Ordner wiedergeben"));
        currentTrack = 1;
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      // Party Modus: Ordner in zufälliger Reihenfolge
      if (myCard.mode == 3) {
        Serial.println(
            F("Party Modus -> Ordner in zufälliger Reihenfolge wiedergeben"));
        currentTrack = random(1, numTracksInFolder + 1);
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      // Einzel Modus: eine Datei aus dem Ordner abspielen
      if (myCard.mode == 4) {
        Serial.println(
            F("Einzel Modus -> eine Datei aus dem Odrdner abspielen"));
        currentTrack = myCard.special;
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
      // Hörbuch Modus: kompletten Ordner spielen und Fortschritt merken
      if (myCard.mode == 5) {
        Serial.println(F("Hörbuch Modus -> kompletten Ordner spielen und "
                         "Fortschritt merken"));
        currentTrack = EEPROM.read(myCard.folder);
        mp3.playFolderTrack(myCard.folder, currentTrack);
      }
    }

    // Neue Karte konfigurieren
    else {
      knownCard = false;
      setupCard();
    }
  }
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

/*
 *
 */
int voiceMenu(
  int numberOfOptions,
  int startMessage,
  int messageOffset,
  bool preview,
  int previewFromFolder
) {
  int returnValue = 0;
  if (startMessage != 0)
    mp3.playMp3FolderTrack(startMessage);
  do {
    pauseButton.read();
    upButton.read();
    downButton.read();
    mp3.loop();
    if (pauseButton.wasPressed()) {
      if (returnValue != 0)
        return returnValue;
      delay(1000);
    }

    if (upButton.pressedFor(LONG_PRESS)) {
      returnValue = min(returnValue + 10, numberOfOptions);
      mp3.playMp3FolderTrack(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
      ignoreUpButton = true;
    } else if (upButton.wasReleased()) {
      if (!ignoreUpButton) {
        returnValue = min(returnValue + 1, numberOfOptions);
        mp3.playMp3FolderTrack(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            mp3.playFolderTrack(returnValue, 1);
          else
            mp3.playFolderTrack(previewFromFolder, returnValue);
        }
      } else
        ignoreUpButton = false;
    }

    if (downButton.pressedFor(LONG_PRESS)) {
      returnValue = max(returnValue - 10, 1);
      mp3.playMp3FolderTrack(messageOffset + returnValue);
      delay(1000);
      if (preview) {
        do {
          delay(10);
        } while (isPlaying());
        if (previewFromFolder == 0)
          mp3.playFolderTrack(returnValue, 1);
        else
          mp3.playFolderTrack(previewFromFolder, returnValue);
      }
      ignoreDownButton = true;
    } else if (downButton.wasReleased()) {
      if (!ignoreDownButton) {
        returnValue = max(returnValue - 1, 1);
        mp3.playMp3FolderTrack(messageOffset + returnValue);
        delay(1000);
        if (preview) {
          do {
            delay(10);
          } while (isPlaying());
          if (previewFromFolder == 0)
            mp3.playFolderTrack(returnValue, 1);
          else
            mp3.playFolderTrack(previewFromFolder, returnValue);
        }
      } else
        ignoreDownButton = false;
    }
  } while (true);
}

void resetCard() {
  do {
    pauseButton.read();
    upButton.read();
    downButton.read();

    if (upButton.wasReleased() || downButton.wasReleased()) {
      Serial.print(F("Abgebrochen!"));
      mp3.playMp3FolderTrack(802);
      return;
    }
  } while (!mfrc522.PICC_IsNewCardPresent());

  if (!mfrc522.PICC_ReadCardSerial())
    return;

  Serial.print(F("Karte wird neu Konfiguriert!"));
  setupCard();
}

void setupCard() {
  mp3.pause();
  Serial.print(F("Neue Karte konfigurieren"));

  // Ordner abfragen
  myCard.folder = voiceMenu(99, 300, 0, true);

  // Wiedergabemodus abfragen
  myCard.mode = voiceMenu(6, 310, 310);

  // Hörbuchmodus -> Fortschritt im EEPROM auf 1 setzen
  EEPROM.write(myCard.folder,1);

  // Einzelmodus -> Datei abfragen
  if (myCard.mode == 4)
    myCard.special = voiceMenu(mp3.getFolderTrackCount(myCard.folder), 320, 0,
                               true, myCard.folder);

  // Admin Funktionen
  if (myCard.mode == 6)
    myCard.special = voiceMenu(3, 320, 320);

  // Karte ist konfiguriert -> speichern
  mp3.pause();
  writeCard(myCard);
}

bool readCard(nfcTagObject *nfcTag) {
  bool returnValue = true;
  // Show some details of the PICC (that is: the tag/card)
  Serial.print(F("Card UID:"));
  dump_byte_array(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.println();
  Serial.print(F("PICC type: "));
  MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.println(mfrc522.PICC_GetTypeName(piccType));

  byte buffer[18];
  byte size = sizeof(buffer);

  // Authenticate using key A
  Serial.println(F("Authenticating using key A..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Show the whole sector as it currently is
  Serial.println(F("Current data in sector:"));
  mfrc522.PICC_DumpMifareClassicSectorToSerial(&(mfrc522.uid), &key, sector);
  Serial.println();

  // Read data from the block
  Serial.print(F("Reading data from block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(blockAddr, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    returnValue = false;
    Serial.print(F("MIFARE_Read() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
  }
  Serial.print(F("Data in block "));
  Serial.print(blockAddr);
  Serial.println(F(":"));
  dump_byte_array(buffer, 16);
  Serial.println();
  Serial.println();

  uint32_t tempCookie;
  tempCookie = (uint32_t) buffer[0] << 24;
  tempCookie += (uint32_t) buffer[1] << 16;
  tempCookie += (uint32_t) buffer[2] << 8;
  tempCookie += (uint32_t) buffer[3];

  nfcTag->cookie = tempCookie;
  nfcTag->version = buffer[4];
  nfcTag->folder = buffer[5];
  nfcTag->mode = buffer[6];
  nfcTag->special = buffer[7];

  return returnValue;
}

void writeCard(nfcTagObject nfcTag) {
  MFRC522::PICC_Type mifareType;
  byte buffer[16] = {0x13, 0x37, 0xb3, 0x47, // 0x1337 0xb347 magic cookie to
                                             // identify our nfc tags
                     0x01,                   // version 1
                     nfcTag.folder,          // the folder picked by the user
                     nfcTag.mode,    // the playback mode picked by the user
                     nfcTag.special, // track or function for admin cards
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

  byte size = sizeof(buffer);

  mifareType = mfrc522.PICC_GetType(mfrc522.uid.sak);

  // Authenticate using key B
  Serial.println(F("Authenticating again using key B..."));
  status = (MFRC522::StatusCode)mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailerBlock, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("PCD_Authenticate() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
    mp3.playMp3FolderTrack(401);
    return;
  }

  // Write data to the block
  Serial.print(F("Writing data into block "));
  Serial.print(blockAddr);
  Serial.println(F(" ..."));
  dump_byte_array(buffer, 16);
  Serial.println();
  status = (MFRC522::StatusCode)mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print(F("MIFARE_Write() failed: "));
    Serial.println(mfrc522.GetStatusCodeName(status));
      mp3.playMp3FolderTrack(401);
  }
  else
    mp3.playMp3FolderTrack(400);
  Serial.println();
  delay(100);
}

/**
 * Helper routine to dump a byte array as hex values to Serial.
 */
void dump_byte_array(byte *buffer, byte bufferSize) {
  for (byte i = 0; i < bufferSize; i++) {
    Serial.print(buffer[i] < 0x10 ? " 0" : " ");
    Serial.print(buffer[i], HEX);
  }
}
