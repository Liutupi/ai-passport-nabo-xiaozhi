/* Helix MP3 API adapter. The third-party decoder retains its RPSL/RCSL notices;
 * see third_party/helix-mp3/LICENSE.txt and README.NABO.md for source details.
 * Helix DNA Client technology included. Copyright (c) RealNetworks, Inc.,
 * 1995-2002. All rights reserved. Covered source accompanies this project.
 */
#include "nabo_radio_decoder.h"
#include "mp3dec.h"
#include <stddef.h>
#include <string.h>

bool nabo_radio_decoder_init(nabo_radio_decoder_t *decoder)
{
    if (!decoder) return false;
    decoder->handle = MP3InitDecoder();
    return decoder->handle != NULL;
}

void nabo_radio_decoder_free(nabo_radio_decoder_t *decoder)
{
    if (!decoder) return;
    if (decoder->handle) MP3FreeDecoder(decoder->handle);
    decoder->handle = NULL;
}

int nabo_radio_decoder_decode(nabo_radio_decoder_t *decoder, const uint8_t *input,
                              int bytes, int16_t *pcm, nabo_radio_frame_info_t *info)
{
    if (!info) return 0;
    memset(info, 0, sizeof(*info));
    if (!decoder || !decoder->handle || !input || !pcm || bytes <= 0) return 0;

    int offset = MP3FindSyncWord((unsigned char *)input, bytes);
    if (offset < 0) {
        /* Preserve the final byte in case it starts a split sync word. */
        info->frame_bytes = bytes > 1 ? bytes - 1 : 0;
        return 0;
    }
    unsigned char *frame = (unsigned char *)input + offset;
    int available = bytes - offset;
    info->frame_bytes = offset;
    /* GetNextFrameInfo reads the optional two-byte CRC as well as the header. */
    if (available < 6) return 0;
    unsigned version = (frame[1] >> 3) & 3;
    unsigned layer = (frame[1] >> 1) & 3;
    unsigned bitrate_index = frame[2] >> 4;
    unsigned sample_rate_index = (frame[2] >> 2) & 3;
    /* The fixed 64k catalog does not use free-bitrate frames. Reject their
     * unbounded next-sync search and invalid headers before entering Helix. */
    if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 ||
        sample_rate_index == 3) {
        info->frame_bytes = offset + 1;
        return 0;
    }
    MP3FrameInfo header = {0};
    if (MP3GetNextFrameInfo(decoder->handle, &header, frame) != ERR_MP3_NONE ||
        header.layer != 3 || header.samprate < 8000 || header.samprate > 48000 ||
        header.bitrate <= 0 || (header.nChans != 1 && header.nChans != 2)) {
        info->frame_bytes = offset + 1;
        return 0;
    }
    int frame_size = ((version == 3 ? 144 : 72) * header.bitrate) / header.samprate;
    frame_size += (frame[2] >> 1) & 1;
    if (frame_size < 6 || frame_size > 2048) {
        info->frame_bytes = offset + 1;
        return 0;
    }
    /* Helix otherwise parses side info before discovering input underflow.
     * Preflight the complete frame so neither source nor reservoir is lost. */
    if (available < frame_size) return 0;
    unsigned char *cursor = frame;
    int remaining = frame_size;
    int result = MP3Decode(decoder->handle, &cursor, &remaining, pcm, 0);
    if (result == ERR_MP3_INDATA_UNDERFLOW) return 0;
    if (result == ERR_MP3_MAINDATA_UNDERFLOW) {
        info->frame_bytes = offset + frame_size;
        return 0;
    }
    if (result != ERR_MP3_NONE) {
        info->frame_bytes = offset + 1;
        return 0;
    }
    MP3GetLastFrameInfo(decoder->handle, &header);
    if ((header.nChans != 1 && header.nChans != 2) || header.outputSamps <= 0 ||
        header.outputSamps > NABO_RADIO_MAX_SAMPLES_PER_FRAME ||
        header.outputSamps % header.nChans || remaining < 0 || remaining > frame_size) {
        info->frame_bytes = offset + frame_size;
        return 0;
    }
    info->frame_bytes = offset + frame_size - remaining;
    info->channels = header.nChans;
    info->hz = header.samprate;
    info->bitrate_kbps = header.bitrate / 1000;
    return header.outputSamps / header.nChans;
}
