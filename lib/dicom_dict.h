/**
 * Made from: DICOM Standard PS3.6 and PS3.7
 * Version: 2025d
 */

#ifndef DICOM_DICT_H
#define DICOM_DICT_H

#include <stdint.h>

#define DICOM_VERSION "2025d"
#define DICOM_DICT_SIZE 5256
#define DICOM_MASK_DICT_SIZE 88

typedef struct {
    uint32_t tag;          // Tag value
    const char *vr;        // Value Representation
    const char *vm;        // Value Multiplicity
    const char *name;      // Element name
    const char *keyword;   // DICOM keyword
    int is_retired;        // 1 if retired
} dicom_element;

typedef struct {
    const char *tag;
    const char *vr;
    const char *vm;
    const char *name;
    const char *keyword;
    int is_retired;
} dicom_mask_element;

extern const dicom_element dicom_dictionary[DICOM_DICT_SIZE];
extern const dicom_mask_element dicom_mask_dictionary[DICOM_MASK_DICT_SIZE];

const dicom_element *dicom_dict_lookup(uint32_t tag);
const dicom_mask_element *dicom_mask_lookup(uint32_t tag);
const char *dicom_get_name(uint32_t tag);
const char *dicom_get_vr(uint32_t tag);
const char *dicom_get_keyword(uint32_t tag);

#endif // DICOM_DICT_H
