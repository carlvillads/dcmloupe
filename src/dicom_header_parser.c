#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dicom_header_parser.h"
#include "dicom_dict.h"
#include "dicom_display.h"

#define DICOM_PREAMBLE_SIZE 128
#define DICOM_PREFIX_SIZE 4
#define DICOM_PREFIX "DICM"
#define TS_IMPLICIT_VR_LITTLE_ENDIAN "1.2.840.10008.1.2"
#define TS_EXPLICIT_VR_LITTLE_ENDIAN "1.2.840.10008.1.2.1"
#define TS_EXPLICIT_VR_BIG_ENDIAN "1.2.840.10008.1.2.2"

static int global_terminal_width = 0;
static int global_val_col_start = 108; // start of VALUE column

static void init_terminal_width(void) {
#ifdef _WIN32
    global_terminal_width = 90;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) { global_terminal_width = ws.ws_col; }
    else { global_terminal_width = 90; }
#endif
}

typedef enum {
    TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN,
    TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN,
    TRANSFER_EXPLICIT_VR_BIG_ENDIAN,
} transfer_syntax_type;

typedef struct {
    transfer_syntax_type ts_type;
    bool is_explicit_vr;
    bool is_little_endian;
    bool collapse_sequences;
    int max_sq_depth;
    bool overwrite_max_disp_len;
} parser_state;

static int parse_data_elements(FILE* fp, const parser_state* state, int depth, int max_elements, int* element_count,
                               const tag_filter* filter);

static display_context create_display_context(const parser_state* state) {
    return (display_context){
        .is_little_endian = state->is_little_endian,
        .overwrite_max_disp_len = state->overwrite_max_disp_len,
        .terminal_width = global_terminal_width,
        .val_col_start = global_val_col_start,
    };
}

static bool should_disp_tag(uint32_t tag, const tag_filter* filter) {
    if (filter == NULL || filter->count == 0) { return true; }
    for (int i = 0; i < filter->count; i++) { if (filter->tags[i] == tag) return true; }
    return false;
}

static uint16_t read_uint16_le(FILE* fp) {
    uint8_t buf[2];
    if (fread(buf, 1, 2, fp) != 2) { return 0; }
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint16_t read_uint16_be(FILE* fp) {
    uint8_t buf[2];
    if (fread(buf, 1, 2, fp) != 2) { return 0; }
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static uint32_t read_uint32_le(FILE* fp) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, fp) != 4) { return 0; }
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
        ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static uint32_t read_uint32_be(FILE* fp) {
    uint8_t buf[4];
    if (fread(buf, 1, 4, fp) != 4) { return 0; }
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
        ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static uint16_t read_uint16(FILE* fp, const parser_state* state) {
    return state->is_little_endian ? read_uint16_le(fp) : read_uint16_be(fp);
}

static uint32_t read_uint32(FILE* fp, const parser_state* state) {
    return state->is_little_endian ? read_uint32_le(fp) : read_uint32_be(fp);
}

static int is_explicit_vr_long(const char* vr) {
    if (vr == NULL || strlen(vr) != 2) { return 0; }
    return (strcmp(vr, "OB") == 0 || strcmp(vr, "OD") == 0 ||
        strcmp(vr, "OF") == 0 || strcmp(vr, "OL") == 0 ||
        strcmp(vr, "OW") == 0 || strcmp(vr, "SQ") == 0 ||
        strcmp(vr, "UC") == 0 || strcmp(vr, "UN") == 0 ||
        strcmp(vr, "UR") == 0 || strcmp(vr, "UT") == 0);
}

static int is_valid_vr(const char* vr) {
    if (vr == NULL || strlen(vr) != 2) { return 0; }

    for (int i = 0; i < 2; i++) {
        if (!((vr[i] >= 'A' && vr[i] <= 'Z') || (vr[i] >= '0' && vr[i] <= '9'))) { return 0; }
    }

    const char* valid_vrs[] = {
        "AE", "AS", "AT", "CS", "DA", "DS", "DT", "FD", "FL", "IS", "LO", "LT",
        "OB", "OD", "OF", "OL", "OW", "PN", "SH", "SL", "SQ", "SS", "ST", "TM",
        "UC", "UI", "UL", "UN", "UR", "US", "UT"
    };

    for (size_t i = 0; i < sizeof(valid_vrs) / sizeof(valid_vrs[0]); i++) {
        if (strcmp(vr, valid_vrs[i]) == 0) return 1;
    }

    return 0;
}

static void print_indent(const int depth) { for (int i = 0; i < depth * 2; i++) { printf("  "); } }

static int count_sequence_items(FILE* fp, const parser_state* state, long* end_pos) {
    int item_count = 0;
    *end_pos = ftell(fp);

    while (!feof(fp)) {
        const long current_pos = ftell(fp);
        const uint16_t group = read_uint16(fp, state);
        const uint16_t element = read_uint16(fp, state);
        if (feof(fp)) break;

        if (group == 0xFFFE) {
            const uint32_t length = read_uint32(fp, state);

            if (element == 0xE0DD) {
                *end_pos = ftell(fp);
                break;
            }
            else if (element == 0xE000) {
                item_count++;

                if (length == 0xFFFFFFFF) {
                    while (!feof(fp)) {
                        const uint16_t g = read_uint16(fp, state);
                        const uint16_t e = read_uint16(fp, state);
                        if (feof(fp)) break;

                        if (g == 0xFFFE && e == 0xE00D) {
                            read_uint32(fp, state);
                            break;
                        }

                        uint32_t tag_len;
                        char vr[3] = {0};

                        if (state->is_explicit_vr) {
                            if (fread(vr, 1, 2, fp) != 2) break;
                            if (is_explicit_vr_long(vr)) {
                                fseek(fp, 2, SEEK_CUR);
                                tag_len = read_uint32(fp, state);
                            }
                            else { tag_len = read_uint16(fp, state); }
                        }
                        else {
                            tag_len = read_uint32(fp, state);
                            const uint32_t tag = ((uint32_t)g << 16) | e;
                            const char* dict_vr = dicom_get_vr(tag);
                            if (dict_vr != NULL) { strncpy(vr, dict_vr, 2); }
                            else { strcpy(vr, "UN"); }
                        }

                        if (strcmp(vr, "SQ") == 0) {
                            if (tag_len == 0xFFFFFFFF) {
                                long nested_end;
                                count_sequence_items(fp, state, &nested_end);
                            }
                            else if (tag_len > 0) { fseek(fp, tag_len, SEEK_CUR); }
                        }
                        else if (tag_len > 0 && tag_len != 0xFFFFFFFF) { fseek(fp, tag_len, SEEK_CUR); }
                    }
                }
                else if (length > 0) { fseek(fp, length, SEEK_CUR); }
                *end_pos = ftell(fp);
                continue;
            }
        }
        else {
            fseek(fp, current_pos, SEEK_SET);
            break;
        }
    }

    return item_count;
}


static int parse_sequence(FILE* fp, const parser_state* state, const int depth, const int max_elements,
                          int* element_count, const tag_filter* filter) {
    if (depth > state->max_sq_depth) {
        long sq_end;
        const int item_count = count_sequence_items(fp, state, &sq_end);

        print_indent(depth - 1);
        if (item_count == 0) { printf("[EMPTY SEQUENCE ABOVE MAX DEPTH]"); }
        else {
            printf("[%d ITEM%s ABOVE MAX SEQUENCE DEPTH]\n",
                   item_count, item_count == 1 ? "" : "S");
        }
        return -1;
    }

    while (!feof(fp) && *element_count < max_elements) {
        const uint16_t group = read_uint16(fp, state);
        const uint16_t element = read_uint16(fp, state);

        if (feof(fp)) { return -1; }

        if (group == 0xFFFE) {
            const uint32_t length = read_uint32(fp, state);

            if (element == 0xE0DD) {
                print_indent(depth);
                printf("(FFFE,E0DD)  --  %-8u %-40s %-45s %s\n",
                       0, "--", "Sequence Delimiter Item", "(end sequence)");
                (*element_count)++;
                return 0;
            }
            else if (element == 0xE000) {
                print_indent(depth);
                if (length == 0xFFFFFFFF) {
                    printf("(FFFE,E000)  %-3s %-8s %-40s %-45s %s\n",
                           "--", "undef", "--",
                           "Item (UNDEFINED LENGTH)",
                           "(begin item)");
                }
                else {
                    printf("(FFFE,E000)  %-3s %-8u %-40s %-45s %s\n",
                           "--", length, "--",
                           "Item (DEFINED LENGTH)",
                           "(begin item)");
                }
                (*element_count)++;

                // parse the inside of the sequence
                if (length == 0xFFFFFFFF) {
                    parse_data_elements(fp, state, depth + 1, max_elements, element_count, filter);
                }
                else if (length > 0) {
                    const long start_pos = ftell(fp);
                    parse_data_elements(fp, state, depth + 1, max_elements, element_count, filter);
                    const long end_pos = ftell(fp);
                    const long bytes_read = end_pos - start_pos;

                    if (bytes_read < length) {
                        // Double check position
                        fseek(fp, start_pos + length, SEEK_SET);
                    }
                }

                continue;
            }
            else if (element == 0xE00D) {
                print_indent(depth);
                printf("(FFFE,E00D)  %-3s %-8u %-40s %-45s %s\n",
                       "--", 0, "--", "Item Delimiter", "(end item)");
                (*element_count)++;
                continue;
            }
        }

        fseek(fp, -4, SEEK_CUR); // If regular data element (should not happen in a SQ)
        return 0;
    }

    return 0;
}

static int parse_data_elements(FILE* fp, const parser_state* state, const int depth, const int max_elements,
                               int* element_count, const tag_filter* filter) {
    while (!feof(fp) && *element_count < max_elements) {
        const uint16_t group = read_uint16(fp, state);
        const uint16_t element = read_uint16(fp, state);

        if (feof(fp)) { break; }

        const uint32_t tag = ((uint32_t)group << 16) | element;

        if (depth > 0 && group == 0xFFFE) {
            fseek(fp, -4, SEEK_CUR); // Put the tag back and let parse_sequence handle it
            return 0;
        }

        if (group == 0x7FE0 && element == 0x0010) {
            // Stop at Pixel Data
            print_indent(depth);
            printf("(%04X,%04X)  %-12s %-40s %-45s %s\n",
                   group, element, "OW/OB", "PixelData", "Pixel Data",
                   "(stopping: pixel data encountered)");
            (*element_count)++;
            return 1;
        }

        char vr[3] = {0};
        uint32_t length;

        if (state->is_explicit_vr) {
            if (fread(vr, 1, 2, fp) != 2) { break; }

            if (!is_valid_vr(vr)) {
                fprintf(stderr, "Warning: Invalid VR '%c%c' at tag (%04X,%04X), skipping\n",
                        vr[0], vr[1], group, element);
                break;
            }

            if (is_explicit_vr_long(vr)) {
                fseek(fp, 2, SEEK_CUR);
                length = read_uint32(fp, state);
            }
            else { length = read_uint16(fp, state); }
        }
        else {
            length = read_uint32(fp, state);
            const char* dict_vr = dicom_get_vr(tag);
            if (dict_vr != NULL) { strncpy(vr, dict_vr, 2); }
            else { strcpy(vr, "UN"); }
        }

        const bool should_display = should_disp_tag(tag, filter);
        if (!should_display && tag != 0x00020010) {
            fseek(fp, length, SEEK_CUR);
            continue;
        }

        const char* name = dicom_get_name(tag);
        const char* dict_vr = dicom_get_vr(tag);
        const char* keyword = dicom_get_keyword(tag);
        const char* actual_vr = state->is_explicit_vr ? vr : (dict_vr ? dict_vr : "UN");

        char disp_keyword_buff[100];
        const char* display_keyword;
        if ((group & 0x0001) && keyword) {
            snprintf(disp_keyword_buff, sizeof(disp_keyword_buff), "[PRIVATE TAG] %s", keyword);
            display_keyword = disp_keyword_buff;
        }
        else if (group & 0x0001) { display_keyword = "[PRIVATE TAG]"; }
        else { display_keyword = keyword ? keyword : "[N/A]"; }

        if (strcmp(actual_vr, "SQ") == 0) {
            print_indent(depth);
            printf("(%04X,%04X)  %-3s %-8s %-40s %-45s ",
                   group, element,
                   actual_vr,
                   "--",
                   display_keyword,
                   name ? name : "[N/A]"
            );

            if (state->collapse_sequences) {
                long seq_end;
                const int item_count = count_sequence_items(fp, state, &seq_end);

                if (item_count == 0) { printf("[EMPTY SEQUENCE]\n"); }
                else { printf("[SEQUENCE with %d ITEM%s]\n", item_count, item_count == 1 ? "" : "S"); }

                (*element_count)++;
                continue;
            }

            if (length == 0xFFFFFFFF) {
                printf("(sequence - undefined length)\n");
                (*element_count)++;
                parse_sequence(fp, state, depth + 1, max_elements, element_count, filter);
            }
            else if (length == 0) {
                printf("(empty sequence)\n");
                (*element_count)++;
            }
            else {
                printf("(sequence - defined length: %u bytes)\n", length);
                (*element_count)++;
                const long start_pos = ftell(fp);
                parse_sequence(fp, state, depth + 1, max_elements, element_count, filter);
                const long end_pos = ftell(fp);
                const long bytes_read = end_pos - start_pos;

                if (bytes_read < length) { fseek(fp, start_pos + length, SEEK_SET); }
            }

            if (length != 0) {
                print_indent(depth);
                printf("%-12s %-3s %-8s %-40s %-45s %s\n",
                       "------------", "---", "--------",
                       "----------------------------------------",
                       "---------------------------------------------",
                       "----------------------------------------");
            }

            continue;
        }

        print_indent(depth);
        printf("(%04X,%04X)  %-3s %-8u %-40s %-45s ",
               group, element,
               actual_vr,
               length,
               display_keyword,
               name ? name : "[N/A]"
        );

        if (length > 0 && length != 0xFFFFFFFF && length < 1024 * 1024) {
            const uint32_t read_len = length < 4096 ? length : 4096;
            uint8_t* value_data = (uint8_t*)malloc(read_len);

            if (value_data != NULL) {
                const size_t bytes_read = fread(value_data, 1, read_len, fp);
                if (bytes_read > 0) {
                    display_context ctx = create_display_context(state);
                    display_value(actual_vr, value_data, bytes_read, depth, &ctx);
                }
                free(value_data);

                if (length > read_len) {
                    if (fseek(fp, length - read_len, SEEK_CUR) != 0) {
                        fprintf(stderr, "\nERROR: Failed to seek in file\n");
                        break;
                    }
                }
            }
            else {
                printf("(memory alloc failed)");
                fseek(fp, length, SEEK_CUR);
            }
        }
        else if (length == 0xFFFFFFFF) { printf("(undefined length - non-sequence)"); }
        else if (length == 0) { printf("(empty)"); }
        else {
            printf("(too large to display)");
            if (fseek(fp, length, SEEK_CUR) != 0) {
                fprintf(stderr, "\nERROR: Failed to seek past large element\n");
                break;
            }
        }

        printf("\n");
        (*element_count)++;
    }

    return 0;
}

int parse_dicom_header(const char* filename, const int max_elements, const bool collapse_sequences,
                       const int max_sq_depth, const bool show_full_values, const tag_filter* filter) {
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return -1;
    }

    uint8_t preamble[DICOM_PREAMBLE_SIZE];
    if (fread(preamble, 1, DICOM_PREAMBLE_SIZE, fp) != DICOM_PREAMBLE_SIZE) {
        fprintf(stderr, "Error: Cannot read file '%s': Invalid DICOM file (header too short)\n", filename);
        fclose(fp);
        return -1;
    }

    char prefix[DICOM_PREFIX_SIZE];
    if (fread(prefix, 1, DICOM_PREFIX_SIZE, fp) != DICOM_PREFIX_SIZE) {
        fprintf(stderr, "Error: Invalid DICOM file (missing DICM prefix from header)\n");
        fclose(fp);
        return -1;
    }

    if (memcmp(prefix, DICOM_PREFIX, DICOM_PREFIX_SIZE) != 0) {
        fprintf(stderr, "Error: Invalid DICM prefix from header\n");
        fclose(fp);
        return -1;
    }

    init_terminal_width();

    // File meta information is always TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN
    parser_state state = {
        .ts_type = TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN,
        .is_explicit_vr = true,
        .is_little_endian = true,
        .collapse_sequences = collapse_sequences,
        .max_sq_depth = max_sq_depth,
        .overwrite_max_disp_len = show_full_values
    };

    printf("DICOM version: %s\n", DICOM_VERSION);
    printf("%-12s %-3s %-8s %-40s %-45s %s\n",
           "TAG", "VR", "LENGTH", "KEYWORD", "NAME", "VALUE");
    printf("%-12s %-3s %-8s %-40s %-45s %s\n",
           "------------", "---", "--------",
           "----------------------------------------",
           "---------------------------------------------",
           "----------------------------------------");

    int element_count = 0;
    char transfer_syntax_uid[65] = {0};
    int in_file_meta = 1;

    while (!feof(fp) && element_count < max_elements) {
        const uint16_t group = read_uint16(fp, &state);
        const uint16_t element = read_uint16(fp, &state);

        if (feof(fp)) { break; }

        const uint32_t tag = ((uint32_t)group << 16) | element;

        if (in_file_meta && group != 0x0002) {
            in_file_meta = 0;

            if (strlen(transfer_syntax_uid) > 0) {
                if (strcmp(transfer_syntax_uid, TS_IMPLICIT_VR_LITTLE_ENDIAN) == 0) {
                    state.ts_type = TRANSFER_IMPLICIT_VR_LITTLE_ENDIAN;
                    state.is_explicit_vr = false;
                    state.is_little_endian = true;
                    printf("\n\t[Transfer Syntax: Implicit VR Little Endian]\n\n");
                }
                else if (strcmp(transfer_syntax_uid, TS_EXPLICIT_VR_BIG_ENDIAN) == 0) {
                    state.ts_type = TRANSFER_EXPLICIT_VR_BIG_ENDIAN;
                    state.is_explicit_vr = true;
                    state.is_little_endian = false;
                    printf("\n\t[Transfer Syntax: Explicit VR Big Endian]\n\n");
                }
                else {
                    state.ts_type = TRANSFER_EXPLICIT_VR_LITTLE_ENDIAN;
                    state.is_explicit_vr = true;
                    state.is_little_endian = true;
                    printf("\n\t[Transfer Syntax: Explicit VR Little Endian]\n\n");
                }
            }
        }

        if (group == 0x7FE0 && element == 0x0010) {
            printf("(%04X,%04X)  %-12s %-40s %-45s %s\n",
                   group, element, "OW/OB", "PixelData", "Pixel Data (Image)",
                   "(pixel data encountered: stopping)");
            for (int i = 0; i < 148; i++) printf("=");
            printf("\n");
            break;
        }

        char vr[3] = {0};
        uint32_t length;

        if (state.is_explicit_vr) {
            if (fread(vr, 1, 2, fp) != 2) { break; }

            if (!is_valid_vr(vr)) {
                fprintf(stderr, "Warning: Invalid VR '%c%c' at tag (%04X,%04X), skipping\n",
                        vr[0], vr[1], group, element);
                break;
            }

            if (is_explicit_vr_long(vr)) {
                fseek(fp, 2, SEEK_CUR); // Skip 2 reserved bytes
                length = read_uint32(fp, &state);
            }
            else { length = read_uint16(fp, &state); }
        }
        else {
            length = read_uint32(fp, &state);
            const char* dict_vr = dicom_get_vr(tag);
            if (dict_vr != NULL) { strncpy(vr, dict_vr, 2); }
            else {
                strcpy(vr, "UN"); // Unknown
            }
        }

        const bool should_display = should_disp_tag(tag, filter);
        if (!should_display && tag != 0x00020010) {
            fseek(fp, length, SEEK_CUR);
            continue;
        }

        const char* name = dicom_get_name(tag);
        const char* dict_vr = dicom_get_vr(tag);
        const char* keyword = dicom_get_keyword(tag);
        const char* actual_vr = state.is_explicit_vr ? vr : (dict_vr ? dict_vr : "UN");
        char disp_keyword_buff[100];
        const char* display_keyword;
        if ((group & 0x0001) && keyword) {
            snprintf(disp_keyword_buff, sizeof(disp_keyword_buff), "[PRIVATE TAG] %s", keyword);
            display_keyword = disp_keyword_buff;
        }
        else if (group & 0x0001) { display_keyword = "[PRIVATE TAG]"; }
        else { display_keyword = keyword ? keyword : "[N/A]"; }

        if (strcmp(actual_vr, "SQ") == 0) {
            printf("(%04X,%04X)  %-3s %-8s %-40s %-45s ",
                   group, element,
                   actual_vr,
                   "--",
                   display_keyword,
                   name ? name : "[N/A]"
            );

            if (state.collapse_sequences) {
                long seq_end;
                const int item_count = count_sequence_items(fp, &state, &seq_end);

                if (item_count == 0) { printf("[EMPTY SEQUENCE]\n"); }
                else { printf("[SEQUENCE with %d ITEM%s]\n", item_count, item_count == 1 ? "" : "S"); }

                element_count++;
                continue;
            }

            if (length == 0xFFFFFFFF) { printf("(sequence - undefined length)\n"); }
            else if (length == 0) { printf("(empty sequence)\n"); }
            else { printf("(sequence - defined length)\n"); }

            element_count++;

            if (length == 0xFFFFFFFF) { parse_sequence(fp, &state, 1, max_elements, &element_count, filter); }
            else if (length > 0) {
                const long start_pos = ftell(fp);
                parse_sequence(fp, &state, 1, max_elements, &element_count, filter);
                const long end_pos = ftell(fp);
                if ((end_pos - start_pos) < length) { fseek(fp, start_pos + length, SEEK_SET); }
            }

            if (length != 0) {
                printf("  %-12s %-3s %-8s %-40s %-45s %s\n",
                       "------------", "---", "--------",
                       "----------------------------------------",
                       "---------------------------------------------",
                       "----------------------------------------");
            }

            continue;
        }

        if (should_display) {
            printf("(%04X,%04X)  %-3s %-8u %-40s %-45s ",
                   group, element,
                   actual_vr,
                   length,
                   display_keyword,
                   name ? name : "[N/A]"
            );
        }

        // Handle trnasfer syntax UID specifically
        if (tag == 0x00020010 && length > 0 && length < sizeof(transfer_syntax_uid)) {
            if (fread(transfer_syntax_uid, 1, length, fp) == length) {
                transfer_syntax_uid[length] = '\0';
                for (uint32_t i = length - 1; i >= 0 &&
                     (transfer_syntax_uid[i] == ' ' || transfer_syntax_uid[i] == '\0'); i--) {
                    transfer_syntax_uid[i] = '\0';
                }

                if (should_display) {
                    display_context ctx = create_display_context(&state);
                    display_value(actual_vr, (uint8_t*)transfer_syntax_uid, strlen(transfer_syntax_uid), 0, &ctx);
                }
            }
        }
        else if (length > 0 && length != 0xFFFFFFFF && length < 1024 * 1024) {
            const uint32_t read_len = length < 4096 ? length : 4096;
            uint8_t* value_data = (uint8_t*)malloc(read_len);

            if (value_data != NULL) {
                const size_t bytes_read = fread(value_data, 1, read_len, fp);
                if (bytes_read > 0) {
                    display_context ctx = create_display_context(&state);
                    display_value(actual_vr, value_data, bytes_read, 0, &ctx);
                }
                free(value_data);

                if (length > read_len) {
                    if (fseek(fp, length - read_len, SEEK_CUR) != 0) {
                        fprintf(stderr, "\nERROR: Failed to seek in file\n");
                        break;
                    }
                }
            }
            else {
                printf("(memory alloc failed)");
                fseek(fp, length, SEEK_CUR);
            }
        }
        else if (length == 0) { printf("(empty)"); }
        else {
            printf("(too large to display)");
            if (fseek(fp, length, SEEK_CUR) != 0) {
                fprintf(stderr, "\nERROR: Failed to seek past large element\n");
                break;
            }
        }

        if (should_display) { printf("\n"); }
        element_count++;
    }

    printf("\n[Parsed %d element%s]\n", element_count, element_count == 1 ? "" : "s");
    fclose(fp);
    return 0;
}
