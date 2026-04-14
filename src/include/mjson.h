/*
 * =====================================================================================
 *       Filename:  mjson.h — MicroJSON parser declarations
 *       Author:    Eric S. Raymond (BSD License)
 *       Modified:  jianxi sun
 * =====================================================================================
 */
#ifndef __MJSON_H__
#define __MJSON_H__

#include <stddef.h>
#include <stdbool.h>

/* Buffer size limits */
#define JSON_ATTR_MAX     64      /* Maximum attribute name length */
#define JSON_VAL_MAX      1024    /* Maximum value string length */

/* Error codes */
enum json_errors {
    JSON_ERR_BADBASE = 1,      /* unknown error while parsing JSON */
    JSON_ERR_OBSTART,          /* non-whitespace when expecting object start */
    JSON_ERR_ATTRSTART,        /* non-whitespace when expecting attribute start */
    JSON_ERR_BADATTR,          /* unknown attribute name */
    JSON_ERR_ATTRLEN,          /* attribute name too long */
    JSON_ERR_NOARRAY,          /* saw [ when not expecting array */
    JSON_ERR_NOBRAK,           /* array element specified, but no [ */
    JSON_ERR_STRLONG,          /* string value too long */
    JSON_ERR_TOKLONG,          /* token value too long */
    JSON_ERR_BADTRAIL,         /* garbage while expecting comma or } or ] */
    JSON_ERR_ARRAYSTART,       /* didn't find expected array start */
    JSON_ERR_OBARRAY,          /* error while parsing object array */
    JSON_ERR_SUBTOOLONG,       /* too many array elements */
    JSON_ERR_BADSUBTRAIL,      /* garbage while expecting array comma */
    JSON_ERR_SUBTYPE,          /* unsupported array element type */
    JSON_ERR_BADSTRING,        /* error while string parsing */
    JSON_ERR_CHECKFAIL,        /* check attribute not matched */
    JSON_ERR_NOPARSTR,         /* can't support strings in parallel arrays */
    JSON_ERR_BADENUM,          /* invalid enumerated value */
    JSON_ERR_QNONSTRING,       /* saw quoted value when expecting nonstring */
    JSON_ERR_NONQSTRING,       /* didn't see quoted value when expecting string */
    JSON_ERR_MISC,             /* other data conversion error */
    JSON_ERR_NULLPTR,          /* unexpected null value or attribute pointer */
};

/* Attribute/array type tags */
enum json_type {
    t_null, t_boolean, t_string, t_integer, t_uinteger,
    t_time, t_real, t_character, t_array, t_object,
    t_structobject, t_check, t_ignore,
};

struct json_enum_t {
    const char *name;
    int value;
};

struct json_attr_t {
    const char *attribute;
    enum json_type type;
    union {
        int *integer;
        unsigned int *uinteger;
        double *real;
        char *string;
        bool *boolean;
        char *character;
        struct json_array_t *array;
        size_t offset;          /* for structobject: offset in struct */
        const char *check;      /* for t_check: expected value */
    } addr;
    union {
        int integer;
        unsigned int uinteger;
        double real;
        char *string;
        bool boolean;
        char character;
        const char *check;      /* for t_check */
    } dflt;
    size_t len;                 /* for strings: max length */
    const struct json_enum_t *map;  /* for enumerated strings */
    int nodefault;              /* suppress assignment of default value */
};

struct json_array_t {
    enum json_type element_type;
    union {
        struct {
            const struct json_attr_t *subtype;
            char *base;         /* base address of struct array */
            size_t stride;      /* sizeof struct */
        } objects;
        struct {
            char *store;        /* string storage area */
            size_t storelen;    /* size of string storage area */
            char **ptrs;        /* pointers to strings */
        } strings;
        struct {
            int *store;
        } integers;
        struct {
            unsigned int *store;
        } uintegers;
        struct {
            double *store;
        } reals;
        struct {
            bool *store;
        } booleans;
    } arr;
    int maxlen;                 /* maximum # of elements */
    int *count;                 /* output: element count */
};

int  json_read_object(const char *cp, const struct json_attr_t *attrs,
                      const char **end);
int  json_read_array(const char *cp, const struct json_array_t *arr,
                     const char **end);
const char *json_error_string(int err);

#endif /* __MJSON_H__ */
