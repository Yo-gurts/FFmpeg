#ifndef FFPLAY_DECODER_H
#define FFPLAY_DECODER_H

#include <SDL.h>
#include <SDL_thread.h>

#include "ffplay_queue.h"

typedef struct FrameData {
    int64_t pkt_pos;
} FrameData;

typedef struct Decoder {
    AVPacket *pkt;
    PacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    SDL_cond *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;
} Decoder;

int decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub);
void decoder_destroy(Decoder *d);

#endif /* FFPLAY_DECODER_H */
