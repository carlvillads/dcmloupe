#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/types.h>

#include "dicom_header_parser.h"

int main(const int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <dicom_file> [options]\n", argv[0]);
        fprintf(stderr, "  <dicom_file>  Path to DICOM file\n");
        fprintf(stderr, "  Options:\n");
        fprintf(stderr, "\t-n <num>     Maximum number of elements to parse (default: 250)\n");
        fprintf(stderr, "\t--all        Parse all elements until start of pixel data\n");
        fprintf(stderr, "\t-d <depth>   Maximum sequence depth (default: 5)\n");
        fprintf(stderr, "\t-c           Collapse sequences\n");
        fprintf(stderr, "\t-v           Show full values (disable truncation)\n");
        fprintf(stderr, "\t-f <tags>    Filter: show only specific tags (format: 0x00100010;0x00080020)\n");

        return 1;
    }

    const char* filename = NULL;
    int max_elements = DEFAULT_MAX_ELEMENTS;
    int max_sq_depth = DEFAULT_MAX_SQ_DEPTH;
    bool collapse_sequences = false;
    bool show_full_values = false;
    tag_filter filter = {.tags = NULL, .count = 0};
    uint32_t tag_array[MAX_FILTER_TAGS];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) { collapse_sequences = true; }
        else if (strcmp(argv[i], "-v") == 0) { show_full_values = true; }
        else if (strcmp(argv[i], "--all") == 0) { max_elements = INT_MAX; }
        else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -n requires a number\n");
                return 1;
            }
            char* endptr;
            const long val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || val <= 0 || val > INT_MAX) {
                fprintf(stderr, "Error: max_elements must be a positive integer\n");
                return 1;
            }
            max_elements = (int)val;
        }
        else if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -d requires a number\n");
                return 1;
            }
            char* endptr;
            const long val = strtol(argv[++i], &endptr, 10);
            if (endptr == argv[i] || *endptr != '\0' || val <= 0 || val > 100) {
                fprintf(stderr, "Error: max_sequence_depth must be between 1 and 100\n");
                return 1;
            }
            max_sq_depth = (int)val;
        }
        else if (strcmp(argv[i], "-f") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -f requires tag(s) in format GGGGEEEE or 0xGGGGEEEE\n");
                return 1;
            }
            i++;
            char* input = argv[i];
            char* token = strtok(input, ",");

            while (token != NULL && filter.count < MAX_FILTER_TAGS) {
                while (*token == ' ' || *token == '\t') token++;

                char* endptr;
                const unsigned long tag_val = strtoul(token, &endptr, 16);

                while (*endptr == ' ' || *endptr == '\t') endptr++;

                if (token == endptr || *endptr != '\0' || tag_val > 0xFFFFFFFF) {
                    fprintf(stderr, "Error: Invalid tag '%s'. Use format: 00100030 or 0x00100030\n", token);
                    return 1;
                }

                tag_array[filter.count++] = (uint32_t)tag_val;
                token = strtok(NULL, ";");
            }

            filter.tags = tag_array;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
        else if (filename == NULL) { filename = argv[i]; }
        else {
            fprintf(stderr, "Error: Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (filename == NULL) {
        fprintf(stderr, "Error: No DICOM file specified\n");
        return 1;
    }

    return parse_dicom_header(filename, max_elements, collapse_sequences, max_sq_depth, show_full_values, &filter);
}
