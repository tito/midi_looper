/**
   Midi looper
   -----------

   .. author:: Mathieu Virbel <mat@txzone.net>

*/


#include "Adafruit_NeoTrellis.h"
#include <MIDI.h>
#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>

#define Y_DIM 8
#define X_DIM 8

#define M_NONE 0
#define M_RECORD 2
#define M_PLAY 1
#define M_LOAD 3
#define M_SAVE 4

#define C_TRACK 0
#define C_CLOCKDIV 1

#define S_TRACK 0
#define S_MEMORY 1

#define NOTES_PER_TRACK 512
#define CHANNELS 6

#define CANCOLOR(_screen, _cmd) (screen == _screen && cmd == _cmd)
#define _U_ __attribute__((unused))

uint32_t OFF = seesaw_NeoPixel::Color(0, 0, 0);
uint32_t RED = seesaw_NeoPixel::Color(255, 0, 0);
uint32_t YELLOW = seesaw_NeoPixel::Color(255, 150, 0);
uint32_t GREEN = seesaw_NeoPixel::Color(0, 255, 0);
uint32_t CYAN = seesaw_NeoPixel::Color(0, 255, 255);
uint32_t BLUE = seesaw_NeoPixel::Color(0, 0, 255);
uint32_t BLUE_DIMMED = seesaw_NeoPixel::Color(0, 0, 30);
uint32_t PURPLE = seesaw_NeoPixel::Color(180, 0, 255);
uint32_t PURPLE_DIMMED = seesaw_NeoPixel::Color(18, 0, 25);
uint32_t WHITE_DIMMED = seesaw_NeoPixel::Color(10, 10, 10);

MIDI_CREATE_DEFAULT_INSTANCE();

Adafruit_NeoTrellis trellis_arr[4] = {
  Adafruit_NeoTrellis(0x30), Adafruit_NeoTrellis(0x2E),
  Adafruit_NeoTrellis(0x32), Adafruit_NeoTrellis(0x2F)
};

Adafruit_MultiTrellis trellis = Adafruit_MultiTrellis(
                                  (Adafruit_NeoTrellis *)trellis_arr, Y_DIM / 4, X_DIM / 4);



#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;
#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);
#else
#error No QSPI/SPI flash are defined on your board variant.h !
#endif

Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem fatfs;

void color(uint8_t x, uint8_t y, uint32_t _color);
extern uint8_t cmd;
extern uint8_t screen;

class Track {
  public:
    uint8_t channel;
    uint8_t notes[NOTES_PER_TRACK];
    uint8_t gatelen;
    uint8_t recidx;
    int8_t clockidx;
    int8_t playidx;
    int8_t clockdiv;
    int8_t ledidx;
    uint8_t x;
    uint8_t y;
    uint8_t gateidx;
    bool playing;
    bool stopend;
    bool resetNext;

    Track() {
      this->playing = false;
      this->stopend = false;
      this->resetNext = false;
      this->gatelen = 3;
      this->channel = 0;
      this->y = 0;
      this->recidx = 0;
      this->playidx = -1;
      this->clockidx = -1;
      this->ledidx = 0;
      this->gateidx = 0;
      this->clockdiv = 8;
      this->playing = false;
    }

    void init(uint8_t x, uint8_t y, uint8_t channel) {
      this->channel = channel;
      this->x = x;
      this->y = y;
    }

    void startRecord() {
      this->resetNext = true;
    }

    void stopRecord() {
      this->resetNext = false;
    }

    void record(uint8_t note) {
      if (this->resetNext) {
        this->recidx = 0;
        this->resetNext = false;
      }

      this->notes[this->recidx] = note;
      this->recidx ++;
      if (this->recidx == (uint8_t)NOTES_PER_TRACK) {
        this->recidx -= 1;
      }
    }

    void start() {
      this->resetNext = false;
      this->playidx = -1;
      this->clockidx = -1;
      this->stopend = false;
      if (this->recidx == 0) return;
      this->playing = true;
    }

    void stop() {
      if (this->notes[this->playidx] != 0) {
        MIDI.sendNoteOff(this->notes[this->playidx], 0, this->channel);
      }
      this->resetNext = false;
      this->playing = false;
      this->stopend = false;
      this->gateidx = 0;
      if (CANCOLOR(S_TRACK, C_TRACK)) {
        color(this->x, this->y, this->isEmpty() ? OFF : BLUE_DIMMED);
      }
    }

    bool tick() {
      if (!this->playing) return false;

      // led off ?
      if (this->ledidx > 0) {
        this->ledidx --;
        if (this->ledidx == 1 && CANCOLOR(S_TRACK, C_TRACK)) {
          color(this->x, this->y, BLUE_DIMMED);
        }
      }

      // gate off ?
      if (this->gateidx > 0) {
        this->gateidx --;
        if (this->gateidx == 0) {
          MIDI.sendNoteOff(this->notes[this->playidx], 127, this->channel);
        }
      }

      // wait a tick that match the clock div
      this->clockidx = (this->clockidx + 1) % 192;
      if ((this->clockidx % this->clockdiv) != 0) {
        return false;
      }

      // generate a note.
      this->playidx = (this->playidx + 1) % this->recidx;

      if (this->stopend && this->playidx == 0) {
        this->stop();
        return true;
      }

      // ensure previous note is off
      if (this->gateidx > 0 && this->notes[this->playidx] != 0) {
          MIDI.sendNoteOff(this->notes[this->playidx], 127, this->channel);
      }

      if (this->notes[this->playidx] != 0) {
        MIDI.sendNoteOn(this->notes[this->playidx], 127, this->channel);
      }
      this->gateidx = this->gatelen;

      #if DEBUG
        Serial.print("PLAY channel=");
        Serial.print(this->channel);
        Serial.print(" pitch=");
        Serial.println(this->notes[this->playidx]);
      #endif
      
      if (CANCOLOR(S_TRACK, C_TRACK)) {
        color(this->x, this->y, BLUE);
        this->ledidx = 2;
      }
      return false;
    }

    void loadFrom(File &file) {
      this->clockdiv = (uint8_t)file.read();
      this->recidx = (uint8_t)file.read();
      for (int i = 0; i < NOTES_PER_TRACK; i++) {
        this->notes[i] = (uint8_t)file.read();
      }
    }

    void saveTo(File &file) {
      file.write(this->clockdiv);
      file.write(this->recidx);
      for (int i = 0; i < NOTES_PER_TRACK; i++) {
        file.write(this->notes[i]);
      }
    }

    uint8_t getClockDiv() {
      return this->clockdiv;
    }
    void setClockDiv(uint8_t clockdiv) {
      this->clockdiv = clockdiv;
    }
    bool isEmpty() {
      return this->recidx == 0;
    }
    void setStopEnd() {
      this->stopend = true;
    }
    bool haveStopEnd() {
      return this->stopend;
    }
    void cancelStopEnd() {
      this->stopend = false;
    }
};

uint8_t DIVS[8] = {128, 96, 48, 24, 12, 8, 6, 4};
uint8_t mode;
uint8_t cmd;
uint8_t screen = S_TRACK;
bool tr_need_update = false;
uint32_t tr_pixels[X_DIM * Y_DIM] = {0};
Track tracks[X_DIM * CHANNELS];
Track *selectedTrack = NULL;
Track *activeTracks[CHANNELS] = {NULL};
Track *nextTracks[CHANNELS] = {NULL};
bool midiPlaying = false;
bool memoryState[X_DIM * Y_DIM] = {false};
File songfile;


void color(uint8_t x, uint8_t y, uint32_t _color) {
  uint32_t c = tr_pixels[x + y * X_DIM];
  if (c == _color) return;
  tr_pixels[x + y * X_DIM] = _color;
  trellis.setPixelColor(x, y, _color);
  tr_need_update = true;
}

void show() {
  if (!tr_need_update) return;
  trellis.show();
  tr_need_update = false;
}

void clearScreen() {
  for (int y = 0; y < Y_DIM; y++) {
    for (int x = 0; x < X_DIM; x++) {
      trellis.setPixelColor(x, y, 0);
      tr_pixels[x + y * X_DIM] = 0;
    }
  }
  trellis.show();
}

void refreshScreenTrack() {
  Track *track;
  uint32_t c;

  color(0, 0, mode == M_RECORD ? RED : GREEN);
  color(1, 0, OFF);

  for (uint8_t x = 0; x < X_DIM; x++) {
    color(x, 1, cmd == x ? BLUE : OFF);
  }

  if (cmd == C_TRACK) {
    for (uint8_t channel = 0; channel < CHANNELS; channel++) {
      for (uint8_t x = 0; x < X_DIM; x++) {
        track = getTrack(channel, x);
        c = OFF;
        if (track->x == x) {
          if (mode == M_RECORD && track == selectedTrack) {
            c = RED;
          } else if (track == nextTracks[channel]) {
            c = PURPLE_DIMMED;
          } else if (!track->isEmpty()) {
            c = BLUE_DIMMED;
          }
        }
        color(x, channel + 2, c);
      }
    }
  } else if (cmd == C_CLOCKDIV) {
    for (uint8_t channel = 0; channel < CHANNELS; channel++) {
      for (uint8_t x = 0; x < X_DIM; x++) {
        color(
          x, channel + 2,
          (DIVS[x] == activeTracks[channel]->getClockDiv()) ? BLUE : OFF);
      }
    }
  } else {
    for (uint8_t channel = 0; channel < CHANNELS; channel++) {
      for (uint8_t x = 0; x < X_DIM; x++) {
        color(x, channel + 2, OFF);
      }
    }
  }
}

void refreshScreenMemory() {
  uint32_t c = mode == M_LOAD ? GREEN : RED;
  color(0, 0, 0);
  color(1, 0, c);
  for (uint8_t x = 0; x < X_DIM; x++) {
    color(x, 1, 0);
  }
  for (uint8_t channel = 0; channel < CHANNELS; channel++) {
    for (uint8_t x = 0; x < X_DIM; x++) {
      color(x, channel + 2, memoryState[channel * X_DIM + x] ? c : OFF);
    }
  }
}

void refreshScreen() {
  if (screen == S_TRACK) {
    refreshScreenTrack();
  } else if (screen == S_MEMORY) {
    refreshScreenMemory();
  }
}

void setMode(uint8_t _mode) {
  mode = _mode;
}

void toggleMode(uint8_t _mode1, uint8_t _mode2) {
  setMode(mode == _mode1 ? _mode2 : _mode1);
}

void setCommand(uint8_t _cmd) {
  cmd = _cmd;
}

void setScreen(uint8_t _screen) {
  screen = _screen;
  if (screen == S_TRACK) {
    setMode(M_PLAY);
  } else if (screen == S_MEMORY) {
    setMode(M_LOAD);
  }
}

void initTracks() {
  Track *track;
  for (uint8_t channel = 0; channel < CHANNELS; channel++) {
    for (uint8_t idx = 0; idx < X_DIM; idx++) {
      track = &tracks[channel * X_DIM + idx];
      track->init(idx, channel + 2, channel + 1);
      if (activeTracks[channel] == NULL) {
        activeTracks[channel] = track;
      }
    }
  }
  selectedTrack = &tracks[0];
}

Track *getTrack(uint8_t channel, uint8_t idx) {
  return &tracks[channel * X_DIM + idx];
}

void selectTrack(uint8_t channel, uint8_t idx) {
  selectedTrack = getTrack(channel, idx);
  activeTracks[channel] = selectedTrack;
}

void midiStart() {
  MIDI.sendStart();
  for (uint8_t channel = 0; channel < CHANNELS; channel++) {
    activeTracks[channel]->start();
  }
}

void midiStop() {
  MIDI.sendStop();
  for (uint8_t channel = 0; channel < CHANNELS; channel++) {
    activeTracks[channel]->stop();
    nextTracks[channel] = NULL;
  }
}

void midiSync() {
  MIDI.sendClock();
  for (uint8_t channel = 0; channel < CHANNELS; channel++) {
    if (activeTracks[channel]->tick()) {
      if (nextTracks[channel]) {
        activeTracks[channel] = nextTracks[channel];
        nextTracks[channel] = NULL;
        activeTracks[channel]->start();
        activeTracks[channel]->tick();
      }
    }
  }
}

char *getMemoryFilename(uint8_t x, uint8_t y) {
  static char memfn[32];
  sprintf((char *)memfn, "song_%d_%d.dat", y, x);
  return memfn;
}

void loadTracksState() {
  char *fn;
  for (uint8_t x = 0; x < X_DIM; x++) {
    for (uint8_t y = 0; y < CHANNELS; y++) {
      fn = getMemoryFilename(x, y);
      Serial.print("Check ");
      Serial.print(fn);
      Serial.print(" = ");
      memoryState[x + y * X_DIM] = fatfs.exists(fn);
      Serial.println(memoryState[x + y * X_DIM] ? "YES" : "-");
    }
  }
}

void saveTracks(uint8_t x, uint8_t y) {
  char *fn = getMemoryFilename(x, y);
  midiStop();
  songfile = fatfs.open(fn, FILE_WRITE);
  songfile.write(0x01);
  for (uint8_t idx = 0; idx < CHANNELS * X_DIM; idx++) {
    tracks[idx].saveTo(songfile);
  }
  Serial.println(songfile.position());
  songfile.close();
  Serial.print("Saved at ");
  Serial.println(fn);
  memoryState[x + y * X_DIM] = true;
  refreshScreen();
}

void loadTracks(uint8_t x, uint8_t y) {
  char *fn = getMemoryFilename(x, y);
  midiStop();
  Serial.print("Loading ");
  Serial.println(fn);
  songfile = fatfs.open(fn, FILE_READ);
  if (!songfile) {
    Serial.print("No file named ");
    Serial.println(fn);
    songfile.close();
    return;
  }
  if (songfile.read() != 0x01) {
    Serial.print("Unknown version");
    songfile.close();
    return;
  }
  for (uint8_t idx = 0; idx < CHANNELS * X_DIM; idx++) {
    tracks[idx].loadFrom(songfile);
  }
  songfile.close();
  Serial.println("Loaded !");

  setScreen(S_TRACK);
}

TrellisCallback handleKeypad(keyEvent evt) {
  uint8_t y = evt.bit.NUM >> 3;
  uint8_t x = evt.bit.NUM - (y << 3);
  Track *track;

  if (screen == S_TRACK) {

    if (x == 0 && y == 0) {
      toggleMode(M_RECORD, M_PLAY);
    } else if (x == 7 && y == 0) {
      if (mode == M_RECORD) {
        selectedTrack->record(0);
      }
    } else if (x == 1 && y == 0) {
      setScreen(S_MEMORY);
    } else if (y == 1) {
      setCommand(x);
    } else if (y >= 2) {
      if (cmd == C_TRACK) {
        if (mode == M_RECORD) {
          activeTracks[y - 2]->stopRecord();
          selectTrack(y - 2, x);
          nextTracks[y - 2] = NULL;
          activeTracks[y - 2]->startRecord();
        } else {
          if (midiPlaying) {
            track = getTrack(y - 2, x);
            if (activeTracks[y - 2] == track) {
              nextTracks[y - 2] = NULL;
              // pressed on the current track
              if (track->playing) {
                track->setStopEnd();
              } else {
                track->start();
              }
            } else {
              // going to another track
              activeTracks[y - 2]->setStopEnd();
              nextTracks[y - 2] = getTrack(y - 2, x);
            }
          } else {
            selectTrack(y - 2, x);
            nextTracks[y - 2] = NULL;
          }
        }
      } else if (cmd == C_CLOCKDIV) {
        for (uint8_t idx = 0; idx < X_DIM; idx++) {
          getTrack(y - 2, idx)->setClockDiv(DIVS[x]);
        }
      }
    }
  } else if (screen == S_MEMORY) {
    if (x == 0 && y == 0) {
      setScreen(S_TRACK);
    } else if (x == 1 && y == 0) {
      toggleMode(M_SAVE, M_LOAD);
    } else if (y >= 2) {
      if (mode == M_SAVE) {
        saveTracks(x, y - 2);
      } else if (mode == M_LOAD) {
        loadTracks(x, y - 2);
      }
    }
  }

  refreshScreen();
  return 0;
}

void handleNoteOn(byte channel, byte pitch, byte velocity)
{
  if (mode == M_RECORD) {
    Serial.print("RECORD channel=");
    Serial.print(channel);
    Serial.print(" pitch=");
    Serial.print(pitch);
    Serial.print(" vel=");
    Serial.println(velocity);
    selectedTrack->record(pitch);
  }
   MIDI.sendNoteOn(pitch, velocity, selectedTrack->channel);
}

void handleNoteOff(byte channel _U_, byte pitch, byte velocity)
{
  //Serial.println("MIDI:OFF");
  MIDI.sendNoteOff(pitch, velocity, selectedTrack->channel);
}

void handleClock() {
  midiPlaying = true;
  midiSync();
}

void handleStart() {
  Serial.println("MIDI:START");
  midiPlaying = true;
  midiStart();
}

void handleStop() {
  Serial.println("MIDI:STOP");
  midiPlaying = false;
  midiStop();
}

void setupProgress(int value) {
  for (int x = 0; x < X_DIM; x++) {
    trellis.setPixelColor(x, 4, x <= value ? BLUE : BLUE_DIMMED);
  }
  trellis.show();
}

void setup() {
  Serial.begin(38400);
  Serial.println("init");

  if (!trellis.begin()) {
    Serial.println("could not start trellis");
    while (1);
  }

  for (int y = 0; y < Y_DIM; y++) {
    for (int x = 0; x < X_DIM; x++) {
      trellis.activateKey(x, y, SEESAW_KEYPAD_EDGE_RISING, true);
      //trellis.activateKey(x, y, SEESAW_KEYPAD_EDGE_FALLING, true);
      trellis.registerCallback(x, y, handleKeypad);
      trellis.setPixelColor(x, y, 0x000000);
    }
  }
  trellis.show();

  setupProgress(1);
  
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOff();
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleClock(handleClock);
  MIDI.setHandleStart(handleStart);
  MIDI.setHandleStop(handleStop);

  // Initialize flash library and check its chip ID.
  setupProgress(2);
  if (!flash.begin()) {
    Serial.println("Error, failed to initialize flash chip!");
    while (1);
  }
  Serial.print("Flash chip JEDEC ID: 0x");
  Serial.println(flash.getJEDECID(), HEX);

  setupProgress(3);
  if (!fatfs.begin(&flash)) {
    Serial.println("Error, failed to mount newly formatted filesystem!");
    Serial.println("Was the flash chip formatted with the SdFat_format example?");
    while (1) yield();
  }
  Serial.println("Mounted filesystem!");


  setupProgress(4);
  initTracks();
  setMode(M_RECORD);
  setCommand(C_TRACK);
  setupProgress(5);
  loadTracksState();

  Serial.println("starting");
  setupProgress(6);
  clearScreen();
  refreshScreen();
}

void loop() {
  trellis.read();
  MIDI.read();
  show();
}
