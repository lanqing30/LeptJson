#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL */
#include <errno.h>
#include <math.h>    /* HUGE_VAL */
#ifndef LEPTJSON_H__
#define LEPTJSON_H__

#define lept_init(v) do { (v)->type = LEPT_NULL; } while(0)
#define lept_set_null(v) lept_free(v)

typedef enum { LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPT_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT } lept_type;

typedef struct lept_value lept_value;
typedef struct lept_member lept_member;

struct lept_value {
	union {
		struct { lept_member* m; size_t size; } o;    // ���� 
		struct { lept_value* e; size_t size; } a;     // ���� 
		struct { char* s; size_t len; } s;            // �ַ���
		double n;                                     // ����
	} u;
	lept_type type;
}; // ǰ������֮������Ͳ�����ȥ������

struct lept_member {
	char* k; size_t klen;   /* member key string, key string length */
	lept_value v;           /* member value */
};


enum {
	LEPT_PARSE_OK = 0, //�ɹ��Ľ���
	LEPT_PARSE_EXPECT_VALUE = 1, // �õ��հ�
	LEPT_PARSE_INVALID_VALUE = 2, // ��Ч���ַ�
	LEPT_PARSE_ROOT_NOT_SINGULAR = 3, // һ��ֵ֮�󣬿հ��ַ�֮���������ַ�
	LEPT_PARSE_NUMBER_TOO_BIG = 4, //����ֵ����
	LEPT_PARSE_MISS_QUOTATION_MARK = 5,
	LEPT_PARSE_INVALID_STRING_ESCAPE = 6,
	LEPT_PARSE_INVALID_STRING_CHAR = 7,
	LEPT_PARSE_INVALID_UNICODE_HEX = 8, // ��� \u ���� 4 λʮ����λ����
	LEPT_PARSE_INVALID_UNICODE_SURROGATE = 9, // ���ֻ�иߴ������Ƿȱ�ʹ�������ǵʹ�����ںϷ���㷶Χ
	LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET = 10,
	LEPT_PARSE_MISS_KEY = 11, // ȱ�ټ�ֵ
	LEPT_PARSE_MISS_COLON = 12, // ȱ��ð��
	LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET = 13, // ȱ�ٷֺŻ��ߴ�����
	LEPT_STRINGIFY_OK = 14 // �ַ�����
};


void lept_free(lept_value* v);
lept_type lept_get_type(const lept_value* v);


// ����json�ַ������õ�һ��lept��һ���ڵ㣬�ŵ�v�У����ؽ����Ľ��
int lept_parse(lept_value* v, const char* json);

int lept_get_boolean(const lept_value* v);
void lept_set_boolean(lept_value* v, int b);

double lept_get_number(const lept_value* v);
void lept_set_number(lept_value* v, double n);

const char* lept_get_string(const lept_value* v);
size_t lept_get_string_length(const lept_value* v);
void lept_set_string(lept_value* v, const char* s, size_t len);

size_t lept_get_array_size(const lept_value* v);
lept_value* lept_get_array_element(const lept_value* v, size_t index);

size_t lept_get_object_size(const lept_value* v);
const char* lept_get_object_key(const lept_value* v, size_t index);
size_t lept_get_object_key_length(const lept_value* v, size_t index);
lept_value* lept_get_object_value(const lept_value* v, size_t index);

#endif /* LEPTJSON_H__ */
