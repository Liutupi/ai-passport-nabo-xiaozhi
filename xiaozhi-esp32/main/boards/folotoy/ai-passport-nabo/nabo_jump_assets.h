#pragma once
#define JUMP_PLATFORM_WIDTH 96
#define JUMP_PLATFORM_HEIGHT 96
#define JUMP_PLATFORM_BYTES (96*96*2)
#define JUMP_CHARACTER_OFFSET (6*JUMP_PLATFORM_BYTES)
#define JUMP_CHARACTER_WIDTH 36
#define JUMP_CHARACTER_HEIGHT 64
extern const unsigned char jump_sprites[] __asm__("_binary_jump_sprites_bin_start");
