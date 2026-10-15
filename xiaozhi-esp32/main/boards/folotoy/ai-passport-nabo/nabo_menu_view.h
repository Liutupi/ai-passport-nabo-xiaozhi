#pragma once
// Poster-style menu panel (cream paper, ink, solid accent blocks) shared by the
// tools, settings, status, radio and keyboard pages. The panel exists only
// while a menu page is open; LVGL allocates from the C3's system heap.
//
// Each frame: Begin(), then any number of component calls, then End(). End()
// hides every component that was not drawn this frame. All setters skip
// unchanged values so a static page causes no LVGL invalidation.
#include <cstdint>
#include "lvgl.h"

namespace menu_view {

constexpr uint32_t kPaper = 0xEFEBE2, kInk = 0x161616, kMuted = 0x8C867B, kGrid = 0xE2DDD2,
                   kOrange = 0xFF4A1C, kBlue = 0x2747FF, kPink = 0xE5226B, kYellow = 0xFFC21A,
                   kWhite = 0xFFFFFF;
constexpr int kRowTop = 94, kRowPitch = 31, kRowsVisible = 6, kSlots = 7;

void Build(lv_obj_t* screen);
void Free();
bool Built();

void Begin();
void End();
// English poster title (e.g. "TOOLS"), Chinese subtitle and page accent.
void Header(const char* title, const char* subtitle, uint32_t accent, const char* counter);
int First(int cursor, int count, int visible = kRowsVisible);
// Selectable rows; the cursor row becomes a solid accent block.
void Rows(const char* const* names, int count, int cursor, bool editing, int top = kRowTop,
          int visible = kRowsVisible);
// Right-aligned value for visible slot `slot` of the last Rows()/Info() call.
void Value(int slot, const char* text);
// Label/value rows without a cursor, separated by hairlines.
void Info(const char* const* names, const char* const* values, int count, int top = kRowTop);
void Card(const char* title, const char* subtitle);
void Body(const char* text, int y);
void Big(const char* text, int y);
void Meter(int percent, int y);
void Footer(const char* text);

}  // namespace menu_view
