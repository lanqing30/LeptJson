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
		struct { lept_member* m; size_t size; } o;    // 对象 
		struct { lept_value* e; size_t size; } a;     // 数组 
		struct { char* s; size_t len; } s;            // 字符串
		double n;                                     // 数字
	} u;
	lept_type type;
}; // 前向声明之后，这里就不用再去声明了

struct lept_member {
	char* k; size_t klen;   /* member key string, key string length */
	lept_value v;           /* member value */
};


enum {
	LEPT_PARSE_OK = 0, //成功的解析
	LEPT_PARSE_EXPECT_VALUE = 1, // 得到空白
	LEPT_PARSE_INVALID_VALUE = 2, // 无效的字符
	LEPT_PARSE_ROOT_NOT_SINGULAR = 3, // 一个值之后，空白字符之后还有其他字符
	LEPT_PARSE_NUMBER_TOO_BIG = 4, //返回值过大
	LEPT_PARSE_MISS_QUOTATION_MARK = 5,
	LEPT_PARSE_INVALID_STRING_ESCAPE = 6,
	LEPT_PARSE_INVALID_STRING_CHAR = 7,
	LEPT_PARSE_INVALID_UNICODE_HEX = 8, // 如果 \u 后不是 4 位十六进位数字
	LEPT_PARSE_INVALID_UNICODE_SURROGATE = 9, // 如果只有高代理项而欠缺低代理项，或是低代理项不在合法码点范围
	LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET = 10,
	LEPT_PARSE_MISS_KEY = 11, // 缺少键值
	LEPT_PARSE_MISS_COLON = 12, // 缺少冒号
	LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET = 13, // 缺少分号或者大括号
	LEPT_STRINGIFY_OK = 14 // 字符串化
};


void lept_free(lept_value* v);
lept_type lept_get_type(const lept_value* v);


// 解析json字符串，得到一个lept的一个节点，放到v中，返回解析的结果
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
