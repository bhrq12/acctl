/*
 * =====================================================================================
 *       Filename:  mjson.h — MicroJSON parser declarations
 *       Author:    Eric S. Raymond (BSD License)
 *       Modified:  jianxi sun
 * =====================================================================================
 */
#ifndef __MJSON_H__
#define __MJSON_H__

#include <stdbool.h>

/* Attribute/array type tags */
enum json_type {
	t_null, t_boolean, t_string, t_integer, t_uinteger,
	t_time, t_real, t_character, t_array, t_object,
	t_structobject, t_check, t_ignore,
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
		const char *check;
	} addr;
	union {
		int integer;
		unsigned int uinteger;
		double real;
		char *string;
		bool boolean;
		char character;
	} dflt;
	size_t len;         /* for strings: max length */
	const struct json_enum_t *map;  /* for enumerated strings */
	int nodefault;      /* suppress assignment of default value */
};

struct json_array_t {
	enum json_type element_type;
	union {
		struct {
			const struct json_attr_t *subtype;
			struct {
				const char *store;
				size_t storelen;
				char **ptrs[1];
			} strings;
			int *integers;
			unsigned int *uintegers;
			double *reals;
			bool *booleans;
		} objects;
	} arr;
	int maxlen;   /* maximum # of elements */
	int *count;  /* output: element count */
};

struct json_enum_t {
	const char *name;
	int value;
};

int  json_read_object(const char *cp, const struct json_attr_t *attrs,
	const char **end);
int  json_read_array(const char *cp, const struct json_array_t *arr,
	const char **end);
const char *json_error_string(int err);

#endif /* __MJSON_H__ */
