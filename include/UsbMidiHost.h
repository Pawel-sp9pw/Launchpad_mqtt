#pragma once
#include <Arduino.h>
#include <atomic>
#include <usb/usb_host.h>

struct MidiPacket { uint8_t bytes[4]; };

class UsbMidiHost {
 public:
  bool begin();
  bool connected() const { return connected_.load(); }
  bool receive(MidiPacket &packet);
  bool send(const MidiPacket &packet);
  uint32_t dropped() const { return dropped_.load(); }

 private:
  static void daemonTask(void *argument);
  static void clientTask(void *argument);
  static void clientEvent(const usb_host_client_event_msg_t *event, void *argument);
  static void inputDone(usb_transfer_t *transfer);
  static void outputDone(usb_transfer_t *transfer);
  void open(uint8_t address);
  void close();
  bool submitInput();
  QueueHandle_t incoming_ = nullptr;
  QueueHandle_t outgoing_ = nullptr;
  usb_host_client_handle_t client_ = nullptr;
  usb_device_handle_t device_ = nullptr;
  usb_transfer_t *input_ = nullptr;
  usb_transfer_t *output_ = nullptr;
  uint8_t interface_ = 0;
  uint8_t inputEndpoint_ = 0;
  uint8_t outputEndpoint_ = 0;
  uint16_t inputSize_ = 0;
  uint16_t outputSize_ = 0;
  uint8_t pendingAddress_ = 0;
  bool gone_ = false;
  bool inputPending_ = false;
  bool outputPending_ = false;
  bool interfaceClaimed_ = false;
  std::atomic<bool> connected_{false};
  std::atomic<uint32_t> dropped_{0};
};
