#include <ascii-chat/network/acip/traffic.h>
#include <ascii-chat/network/packet/packet.h>

acip_traffic_class_t acip_packet_traffic_class(uint16_t type) {
  switch (type) {
  case PACKET_TYPE_AUDIO_MESSAGE:
  case PACKET_TYPE_AUDIO_BATCH:
  case PACKET_TYPE_AUDIO_OPUS_BATCH:
    return ACIP_TRAFFIC_AUDIO;
  case PACKET_TYPE_ASCII_FRAME:
  case PACKET_TYPE_IMAGE_FRAME:
  case PACKET_TYPE_IMAGE_FRAME_H264:
  case PACKET_TYPE_IMAGE_FRAME_H265:
    return ACIP_TRAFFIC_VISUAL;
  default:
    return ACIP_TRAFFIC_CONTROL;
  }
}
