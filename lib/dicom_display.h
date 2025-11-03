#ifndef DICOM_DISPLAY_H
#define DICOM_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool is_little_endian;
    bool overwrite_max_disp_len;
    int terminal_width;
    int val_col_start;
} display_context;

void display_value(const char* vr, const uint8_t* data, uint32_t length, int depth, const display_context* ctx);

#endif // DICOM_DISPLAY_H
