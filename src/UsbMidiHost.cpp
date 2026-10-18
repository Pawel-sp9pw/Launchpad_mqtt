#include "UsbMidiHost.h"
#include <esp_intr_alloc.h>

bool UsbMidiHost::begin() {
  incoming_ = xQueueCreate(64, sizeof(MidiPacket));
  outgoing_ = xQueueCreate(256, sizeof(MidiPacket));
  if (!incoming_ || !outgoing_) return false;
  usb_host_config_t hostConfig = {};
  hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&hostConfig) != ESP_OK) return false;
  if (xTaskCreate(daemonTask, "usb-host", 4096, this, 4, nullptr) != pdPASS)
    return false;
  return xTaskCreate(clientTask, "usb-midi", 6144, this, 3, nullptr) == pdPASS;
}

bool UsbMidiHost::receive(MidiPacket &packet) {
  return incoming_ && xQueueReceive(incoming_, &packet, 0) == pdTRUE;
}

bool UsbMidiHost::send(const MidiPacket &packet) {
  if (!connected() || !outgoing_) return false;
  if (xQueueSend(outgoing_, &packet, 0) == pdTRUE) return true;
  ++dropped_;
  return false;
}

void UsbMidiHost::daemonTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

void UsbMidiHost::clientEvent(const usb_host_client_event_msg_t *event, void *argument) {
  auto *host = static_cast<UsbMidiHost *>(argument);
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !host->device_)
    host->pendingAddress_ = event->new_dev.address;
  if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    host->connected_ = false;
    host->gone_ = true;
  }
}

bool UsbMidiHost::submitInput() {
  if (!device_ || gone_) return false;
  input_->num_bytes = inputSize_;
  input_->device_handle = device_;
  input_->bEndpointAddress = inputEndpoint_;
  input_->callback = inputDone;
  input_->context = this;
  inputPending_ = usb_host_transfer_submit(input_) == ESP_OK;
  return inputPending_;
}

void UsbMidiHost::inputDone(usb_transfer_t *transfer) {
  auto *host = static_cast<UsbMidiHost *>(transfer->context);
  host->inputPending_ = false;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && !host->gone_) {
    for (int offset = 0; offset + 4 <= transfer->actual_num_bytes; offset += 4) {
      MidiPacket packet;
      memcpy(packet.bytes, transfer->data_buffer + offset, 4);
      if (xQueueSend(host->incoming_, &packet, 0) != pdTRUE) ++host->dropped_;
    }
  } else if (!host->gone_) {
    host->connected_ = false;
    host->gone_ = true;
  }
}

void UsbMidiHost::outputDone(usb_transfer_t *transfer) {
  auto *host = static_cast<UsbMidiHost *>(transfer->context);
  host->outputPending_ = false;
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    host->connected_ = false;
    host->gone_ = true;
  }
}

void UsbMidiHost::open(uint8_t address) {
  if (usb_host_device_open(client_, address, &device_) != ESP_OK) return;
  const usb_device_desc_t *deviceDescriptor = nullptr;
  const usb_config_desc_t *configuration = nullptr;
  if (usb_host_get_device_descriptor(device_, &deviceDescriptor) != ESP_OK ||
      deviceDescriptor->idVendor != 0x1235 || deviceDescriptor->idProduct != 0x0113 ||
      usb_host_get_active_config_descriptor(device_, &configuration) != ESP_OK) {
    close();
    return;
  }
  const uint8_t *cursor = reinterpret_cast<const uint8_t *>(configuration);
  size_t remaining = configuration->wTotalLength;
  bool midiInterface = false;
  while (remaining >= 2 && cursor[0] >= 2 && cursor[0] <= remaining) {
    if (cursor[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE && cursor[0] >= 9) {
      if (midiInterface && inputEndpoint_ && outputEndpoint_) break;
      midiInterface = cursor[5] == 1 && cursor[6] == 3 && cursor[3] == 0;
      inputEndpoint_ = outputEndpoint_ = 0;
      interface_ = cursor[2];
    } else if (midiInterface && cursor[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT &&
               cursor[0] >= 7 && (cursor[3] & 3) == 2) {
      const uint16_t packetSize = (cursor[4] | (cursor[5] << 8)) & 0x7ff;
      if (packetSize < 4 || packetSize > 512 || packetSize % 4) {
        close();
        return;
      }
      if (cursor[2] & 0x80) {
        inputEndpoint_ = cursor[2];
        inputSize_ = packetSize;
      } else {
        outputEndpoint_ = cursor[2];
        outputSize_ = packetSize;
      }
    }
    remaining -= cursor[0];
    cursor += cursor[0];
  }
  if (!inputEndpoint_ || !outputEndpoint_ ||
      usb_host_interface_claim(client_, device_, interface_, 0) != ESP_OK) {
    close();
    return;
  }
  interfaceClaimed_ = true;
  if (usb_host_transfer_alloc(inputSize_, 0, &input_) != ESP_OK ||
      usb_host_transfer_alloc(outputSize_, 0, &output_) != ESP_OK) {
    close();
    return;
  }
  gone_ = false;
  if (!submitInput()) {
    close();
    return;
  }
  connected_ = true;
}

void UsbMidiHost::close() {
  connected_ = false;
  if (!device_) return;
  gone_ = true;
  if (inputPending_) {
    usb_host_endpoint_halt(device_, inputEndpoint_);
    usb_host_endpoint_flush(device_, inputEndpoint_);
  }
  if (outputPending_) {
    usb_host_endpoint_halt(device_, outputEndpoint_);
    usb_host_endpoint_flush(device_, outputEndpoint_);
  }
  const uint32_t started = millis();
  while ((inputPending_ || outputPending_) && millis() - started < 2000)
    usb_host_client_handle_events(client_, pdMS_TO_TICKS(10));
  if (inputPending_ || outputPending_) return;
  if (input_) usb_host_transfer_free(input_);
  if (output_) usb_host_transfer_free(output_);
  input_ = output_ = nullptr;
  if (interfaceClaimed_) usb_host_interface_release(client_, device_, interface_);
  usb_host_device_close(client_, device_);
  device_ = nullptr;
  interfaceClaimed_ = false;
  inputEndpoint_ = outputEndpoint_ = 0;
  xQueueReset(outgoing_);
  xQueueReset(incoming_);
  gone_ = false;
}

void UsbMidiHost::clientTask(void *argument) {
  auto *host = static_cast<UsbMidiHost *>(argument);
  usb_host_client_config_t clientConfig = {};
  clientConfig.max_num_event_msg = 8;
  clientConfig.async.client_event_callback = clientEvent;
  clientConfig.async.callback_arg = host;
  if (usb_host_client_register(&clientConfig, &host->client_) != ESP_OK) {
    vTaskDelete(nullptr);
    return;
  }
  for (;;) {
    usb_host_client_handle_events(host->client_, pdMS_TO_TICKS(2));
    if (host->gone_) {
      host->close();
      continue;
    }
    if (host->pendingAddress_) {
      const uint8_t address = host->pendingAddress_;
      host->pendingAddress_ = 0;
      host->open(address);
    }
    if (!host->connected()) continue;
    if (!host->inputPending_ && !host->submitInput()) {
      host->gone_ = true;
      continue;
    }
    if (!host->outputPending_) {
      MidiPacket packet;
      int total = 0;
      while (total + 4 <= host->outputSize_ &&
             xQueueReceive(host->outgoing_, &packet, 0) == pdTRUE) {
        memcpy(host->output_->data_buffer + total, packet.bytes, 4);
        total += 4;
      }
      if (total) {
        host->output_->device_handle = host->device_;
        host->output_->bEndpointAddress = host->outputEndpoint_;
        host->output_->num_bytes = total;
        host->output_->callback = outputDone;
        host->output_->context = host;
        host->outputPending_ = usb_host_transfer_submit(host->output_) == ESP_OK;
        if (!host->outputPending_) host->gone_ = true;
      }
    }
  }
}
