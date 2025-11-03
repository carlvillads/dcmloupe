#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include "dicom_display.h"

void display_value(const char* vr, const uint8_t* data, const uint32_t length, const int depth, const display_context* ctx) {
    if (data == NULL || length == 0 || length == 0xFFFFFFFF) {
        printf("(n/a)");
        return;
    }

    const int indent_width = depth * 2 * 2;
    const int max_val_width_int = ctx->overwrite_max_disp_len ? INT_MAX :
        (ctx->terminal_width - ctx->val_col_start - indent_width - 10);  // Account for "" and '...'
    const uint32_t max_val_width = max_val_width_int > 0 ? (uint32_t)max_val_width_int : 20;  // In case of negative number

    if (strcmp(vr, "AE") == 0 || strcmp(vr, "AS") == 0 || strcmp(vr, "CS") == 0 ||
        strcmp(vr, "DA") == 0 || strcmp(vr, "DS") == 0 || strcmp(vr, "DT") == 0 ||
        strcmp(vr, "IS") == 0 || strcmp(vr, "LO") == 0 || strcmp(vr, "LT") == 0 ||
        strcmp(vr, "PN") == 0 || strcmp(vr, "SH") == 0 || strcmp(vr, "ST") == 0 ||
        strcmp(vr, "TM") == 0 || strcmp(vr, "UC") == 0 || strcmp(vr, "UI") == 0 ||
        strcmp(vr, "UR") == 0 || strcmp(vr, "UT") == 0) {
        const uint32_t display_len = length < max_val_width ? length : max_val_width;

        printf("\"");
        for (uint32_t i = 0; i < display_len; i++) {
            if (data[i] >= 32 && data[i] < 127) { putchar(data[i]); }
            else if (data[i] == 0) { break; }
        }
        if (length > max_val_width) { printf("..."); }
        printf("\"");
    }
    else if (strcmp(vr, "US") == 0) {
        if (length >= 2) {
            uint16_t val;
            if (ctx->is_little_endian) { val = (uint16_t)data[0] | ((uint16_t)data[1] << 8); }
            else { val = ((uint16_t)data[0] << 8) | (uint16_t)data[1]; }
            printf("%u", val);
            if (length > 2) { printf(" [+%u more]", (length / 2) - 1); }
        }
    }
    else if (strcmp(vr, "UL") == 0) {
        if (length >= 4) {
            uint32_t val;
            if (ctx->is_little_endian) {
                val = (uint32_t)data[0] |
                    ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16) |
                    ((uint32_t)data[3] << 24);
            }
            else {
                val = ((uint32_t)data[0] << 24) |
                    ((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) |
                    (uint32_t)data[3];
            }
            printf("%u", val);
            if (length > 4) { printf(" [+%u more]", (length / 4) - 1); }
        }
    }
    else if (strcmp(vr, "SS") == 0) {
        if (length >= 2) {
            int16_t val;
            if (ctx->is_little_endian) { val = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8)); }
            else { val = (int16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]); }
            printf("%d", val);
            if (length > 2) { printf(" [+%u more]", (length / 2) - 1); }
        }
    }
    else if (strcmp(vr, "SL") == 0) {
        if (length >= 4) {
            int32_t val;
            if (ctx->is_little_endian) {
                val = (int32_t)((uint32_t)data[0] |
                    ((uint32_t)data[1] << 8) |
                    ((uint32_t)data[2] << 16) |
                    ((uint32_t)data[3] << 24));
            }
            else {
                val = (int32_t)(((uint32_t)data[0] << 24) |
                    ((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) |
                    (uint32_t)data[3]);
            }
            printf("%d", val);
            if (length > 4) { printf(" [+%u more]", (length / 4) - 1); }
        }
    }
    else if (strcmp(vr, "FL") == 0) {
        if (length >= 4) {
            float val;
            if (ctx->is_little_endian) { memcpy(&val, data, sizeof(float)); }
            else {
                const uint8_t reversed[4] = {data[3], data[2], data[1], data[0]};
                memcpy(&val, reversed, sizeof(float));
            }
            printf("%g", val);
            if (length > 4) { printf(" [+%u more]", (length / 4) - 1); }
        }
    }
    else if (strcmp(vr, "FD") == 0) {
        if (length >= 8) {
            double val;
            if (ctx->is_little_endian) { memcpy(&val, data, sizeof(double)); }
            else {
                const uint8_t reversed[8] = {
                    data[7], data[6], data[5], data[4],
                    data[3], data[2], data[1], data[0]
                };
                memcpy(&val, reversed, sizeof(double));
            }
            printf("%g", val);
            if (length > 8) { printf(" [+%u more]", (length / 8) - 1); }
        }
    }
    else if (strcmp(vr, "AT") == 0) {
        if (length >= 4) {
            uint16_t group, elem;
            if (ctx->is_little_endian) {
                group = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
                elem = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
            }
            else {
                group = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
                elem = ((uint16_t)data[2] << 8) | (uint16_t)data[3];
            }
            printf("(%04X,%04X)", group, elem);
            if (length > 4) { printf(" [+%u more]", (length / 4) - 1); }
        }
    }
    else if (strcmp(vr, "SQ") == 0) { printf("(sequence)"); }
    else if (strcmp(vr, "UN") == 0 && length > 0 && length < 256) {
        // tries to interpret unknown tags (typically private ones) as a string to see if we can show something
        uint32_t printable_count = 0;
        for (uint32_t i = 0; i < length && i < 100; i++) {
            if ((data[i] >= 32 && data[i] < 127) || data[i] == '\n' || data[i] == '\r' || data[i] == '\t') {
                printable_count++;
            }
        }
        if (printable_count > (length * 5 / 10)) {
            const uint32_t display_len = length < max_val_width ? length : max_val_width;

            printf("\"");
            for (uint32_t i = 0; i < display_len; i++) {
                if (data[i] >= 32 && data[i] < 127) { putchar(data[i]); }
                else if (data[i] == 0) { break; }
            }
            if (length > max_val_width) { printf("..."); }
            printf("\" [interpreted]");
        } else {
            printf("(binary: %u bytes) ", length);
            const uint32_t show_bytes = length < 8 ? length : 8;
            for (uint32_t i = 0; i < show_bytes; i++) { printf("%02X ", data[i]); }
            if (length > 8) { printf("..."); }
        }
    }
    else if (strcmp(vr, "OB") == 0 || strcmp(vr, "OW") == 0 ||
        strcmp(vr, "OD") == 0 || strcmp(vr, "OF") == 0 ||
        strcmp(vr, "OL") == 0) {
        printf("(binary: %u bytes) ", length);
        const uint32_t show_bytes = length < 8 ? length : 8;

        for (uint32_t i = 0; i < show_bytes; i++) { printf("%02X ", data[i]); }

        if (length > 8) { printf("..."); }
    }
    else { printf("(UNKNOWN VR: %u BYTES)", length); }
}
