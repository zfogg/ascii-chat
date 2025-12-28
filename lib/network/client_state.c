/**
 * @file client_state.c
 * @brief Client state initialization utilities implementation
 *
 * @ingroup network
 */

#include "lib/network/client_state.h"
#include "src/server/client.h"
#include "lib/network/packet.h"
#include <stdatomic.h>

void client_state_enable_video(client_info_t *client) {
    if (!client) {
        return;
    }
    atomic_store(&client->is_sending_video, true);
}

void client_state_enable_audio(client_info_t *client) {
    if (!client) {
        return;
    }
    atomic_store(&client->is_sending_audio, true);
}

void client_state_disable_video(client_info_t *client) {
    if (!client) {
        return;
    }
    atomic_store(&client->is_sending_video, false);
}

void client_state_disable_audio(client_info_t *client) {
    if (!client) {
        return;
    }
    atomic_store(&client->is_sending_audio, false);
}

void client_state_set_streams(client_info_t *client, uint32_t stream_types, bool enable) {
    if (!client) {
        return;
    }

    if (stream_types & STREAM_TYPE_VIDEO) {
        if (enable) {
            client_state_enable_video(client);
        } else {
            client_state_disable_video(client);
        }
    }

    if (stream_types & STREAM_TYPE_AUDIO) {
        if (enable) {
            client_state_enable_audio(client);
        } else {
            client_state_disable_audio(client);
        }
    }
}

void client_state_init_capabilities(client_info_t *client, uint32_t capabilities, uint32_t width, uint32_t height) {
    if (!client) {
        return;
    }

    client->terminal_caps.capabilities = capabilities;
    client->terminal_caps.width = width;
    client->terminal_caps.height = height;
    client->has_terminal_caps = true;
}
