#ifndef DCMLOUPE_DICOM_HEADER_PARSER_H
#define DCMLOUPE_DICOM_HEADER_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define DEFAULT_MAX_ELEMENTS 250
#define DEFAULT_MAX_SQ_DEPTH 5
#define MAX_FILTER_TAGS 100

typedef struct {
    uint32_t* tags;
    int count;
} tag_filter;

int parse_dicom_header(
    const char* filename,
    int max_elements,
    bool collapse_sequences,
    int max_sq_depth,
    bool show_full_values,
    const tag_filter* filter
    );

#endif //DCMLOUPE_DICOM_HEADER_PARSER_H
