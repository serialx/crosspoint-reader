#pragma once

#include <xp_epub_session.h>

extern "C" {
void* xp_epub_test_read_folder_create(const XpEpubLeaves*, const XpAppRuntime*, const XpAppResources*);
void xp_epub_test_read_folder_destroy(void*);
uint8_t xp_epub_test_read_folder_classify(XpAppText);
uint8_t xp_epub_test_read_folder_destination(void*, XpAppText, XpAppTextSink, void*);
uint8_t xp_epub_test_read_folder_move(void*, XpAppText, XpAppText, XpAppText);
}
