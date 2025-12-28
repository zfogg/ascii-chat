/**
 * @file packet_utils.c
 * @brief Packet serialization and deserialization utilities implementation
 *
 * @ingroup network
 */

#include "lib/network/packet_utils.h"
#include <string.h>
#include <arpa/inet.h>

int packet_deserialize_ascii_frame_header(packet_ascii_frame_header_t *header, const void *data, size_t len) {
    if (!header || !data) {
        return -1;
    }

    if (len < sizeof(packet_ascii_frame_header_t)) {
        return -1;
    }

    // Copy header from packet
    memcpy(header, data, sizeof(packet_ascii_frame_header_t));

    // Convert all fields from network byte order
    header->width = ntohl(header->width);
    header->height = ntohl(header->height);
    header->original_size = ntohl(header->original_size);
    header->compressed_size = ntohl(header->compressed_size);
    header->checksum = ntohl(header->checksum);
    header->flags = ntohl(header->flags);

    return 0;
}

int packet_serialize_ascii_frame_header(void *data, size_t len, const packet_ascii_frame_header_t *header) {
    if (!data || !header) {
        return -1;
    }

    if (len < sizeof(packet_ascii_frame_header_t)) {
        return -1;
    }

    // Create a network-byte-order copy of the header
    packet_ascii_frame_header_t network_header;
    network_header.width = htonl(header->width);
    network_header.height = htonl(header->height);
    network_header.original_size = htonl(header->original_size);
    network_header.compressed_size = htonl(header->compressed_size);
    network_header.checksum = htonl(header->checksum);
    network_header.flags = htonl(header->flags);

    // Copy to output buffer
    memcpy(data, &network_header, sizeof(packet_ascii_frame_header_t));

    return 0;
}

int packet_deserialize_dimensions(uint32_t *width, uint32_t *height, const void *data, size_t len) {
    if (!width || !height || !data) {
        return -1;
    }

    if (len < sizeof(uint32_t) * 2) {
        return -1;
    }

    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t w_network, h_network;

    memcpy(&w_network, ptr, sizeof(uint32_t));
    memcpy(&h_network, ptr + sizeof(uint32_t), sizeof(uint32_t));

    *width = ntohl(w_network);
    *height = ntohl(h_network);

    return 0;
}

int packet_serialize_dimensions(void *data, size_t len, uint32_t width, uint32_t height) {
    if (!data) {
        return -1;
    }

    if (len < sizeof(uint32_t) * 2) {
        return -1;
    }

    uint8_t *ptr = (uint8_t *)data;
    uint32_t w_network = htonl(width);
    uint32_t h_network = htonl(height);

    memcpy(ptr, &w_network, sizeof(uint32_t));
    memcpy(ptr + sizeof(uint32_t), &h_network, sizeof(uint32_t));

    return 0;
}
