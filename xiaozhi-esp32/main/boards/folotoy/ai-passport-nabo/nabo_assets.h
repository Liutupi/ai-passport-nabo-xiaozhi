#pragma once
#include <stdint.h>
#define NABO_WIDTH 224
#define NABO_HEIGHT 224
#define NABO_CLIP_COUNT 11
#define NABO_CLIP_HAPPY 4
#define NABO_FRAME_IDLE 2
#define NABO_FRAME_TIRED 18
#define NABO_FRAME_SLEEP 10
typedef struct { uint32_t palette, rows, rle; } nabo_frame_t;
typedef struct { const char *name; uint8_t count, fps, loop; uint8_t frames[6]; } nabo_clip_t;
static const nabo_frame_t nabo_frames[] = {
    {0, 512, 1412},
    {30263, 30775, 31675},
    {57442, 57954, 58854},
    {84085, 84597, 85497},
    {110558, 111070, 111970},
    {142941, 143453, 144353},
    {169579, 170091, 170991},
    {198973, 199485, 200385},
    {225494, 226006, 226906},
    {251648, 252160, 253060},
    {281848, 282360, 283260},
    {308328, 308840, 309740},
    {334617, 335129, 336029},
    {361004, 361516, 362416},
    {387507, 388019, 388919},
    {414106, 414618, 415518},
    {440744, 441256, 442156},
    {468508, 469020, 469920},
    {495207, 495719, 496619},
    {521767, 522279, 523179},
    {548230, 548742, 549642},
    {574578, 575090, 575990},
    {601051, 601563, 602463},
    {627436, 627948, 628848},
    {660570, 661082, 661982},
    {686838, 687350, 688250},
};
static const nabo_clip_t nabo_clips[] = {
    {"idle", 1, 1, 0, {2}},
    {"listen", 2, 1, 0, {7, 8}},
    {"think", 1, 1, 0, {17}},
    {"speak", 4, 8, 1, {11, 14, 12, 15}},
    {"happy", 1, 2, 1, {1}},
    {"wave", 3, 6, 0, {23, 24, 25}},
    {"wake", 4, 4, 0, {19, 20, 21, 22}},
    {"tired", 1, 1, 0, {18}},
    {"sleep", 1, 1, 0, {10}},
    {"sad", 1, 1, 0, {9}},
    {"angry", 1, 1, 0, {0}},
};
static const uint8_t nabo_mode_clips[] = {5, 0, 1, 2, 3, 7, 8};
