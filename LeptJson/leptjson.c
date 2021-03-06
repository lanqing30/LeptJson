#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL,malloc,realloc,free */
#include <errno.h>
#include <math.h>    /* HUGE_VAL */
#include <stdio.h>

#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')
#define escape (%x5C)

#ifndef LEPT_PARSE_STACK_INIT_SIZE
#define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256
#endif

typedef struct {
	const char* json;
	char* stack;
	size_t size, top; // size当前栈的容量，top栈顶的位置
} lept_context;

// 栈的操作，相当于C++ vector
static void* lept_context_push(lept_context* c, size_t size) { // 将栈扩展size大小的容量，必要的时候开辟新的空间
	void* ret;
	assert(size > 0);
	if (c->top + size >= c->size) {
		if (c->size == 0)
			c->size = LEPT_PARSE_STACK_INIT_SIZE;
		while (c->top + size >= c->size)
			c->size += c->size >> 1;  /* c->size * 1.5 */
		c->stack = (char*)realloc(c->stack, c->size);
	}
	ret = c->stack + c->top;
	c->top += size;
	return ret;
}

static void* lept_context_pop(lept_context* c, size_t size) { // 将栈弹出size的容量
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}

// 定义一个宏操作，将栈c中加入一个字符
#define PUTC(c, ch) do { *(char*)lept_context_push(c, sizeof(char)) = (ch); } while(0)


// 释放空间，如果是obj类型，或者数组类型需要递归的释放空间
void lept_free(lept_value* v) {
	assert(v != NULL);
	if (v->type == LEPT_STRING)
		free(v->u.s.s);
	else if (v->type == LEPT_ARRAY) {
		size_t i = 0;
		for (; i < v->u.a.size; i++) lept_free(v->u.a.e + i);
		free(v->u.a.e);
	}
	else if (v->type == LEPT_OBJECT) {
		size_t i = 0;
		for (i = 0; i < v->u.o.size; i++) {
			free(v->u.o.m[i].k);
			lept_free(&v->u.o.m[i].v);
		}
		free(v->u.o.m);
	}
	v->type = LEPT_NULL;
}

///!*********************接下来使一些设置和获取值的函数*******************
// 设置一个值为字符串
void lept_set_string(lept_value* v, const char* s, size_t len) {
	assert(v != NULL && (s != NULL || len == 0));
	lept_free(v);
	v->u.s.s = (char*)malloc(len + 1);
	memcpy(v->u.s.s, s, len);
	v->u.s.s[len] = '\0';
	v->u.s.len = len;
	v->type = LEPT_STRING;
}

int lept_get_boolean(const lept_value* v) {
	assert(v != NULL && (v->type == LEPT_FALSE) || (v->type == LEPT_TRUE));
	if (v->type == LEPT_FALSE) return 0;
	else return 1;
}
void lept_set_boolean(lept_value* v, int b) {
	lept_free(v);
	if (b) {
		v->type = LEPT_TRUE;
	}
	else {
		v->type = LEPT_FALSE;
	}
}

void lept_set_number(lept_value* v, double nx) {
	lept_free(v);
	v->type = LEPT_NUMBER;
	v->u.n = nx;
}

double lept_get_number(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_NUMBER);
	double tmp = v->u.n;
	return tmp;
	//return v->u.n;
}

lept_value* lept_get_array_element(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	assert(index < v->u.a.size);
	return &v->u.a.e[index];
}

size_t lept_get_array_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->u.a.size;
}

const char* lept_get_string(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.s;
}
size_t lept_get_string_length(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.len;
}


size_t lept_get_object_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->u.o.size;
}

const char* lept_get_object_key(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].k;
}

size_t lept_get_object_key_length(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].klen;
}

lept_value* lept_get_object_value(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return &v->u.o.m[index].v;
}


///!*************************接下来是一些解析的函数******************************
// 解析字符串函数开始

/*
	https://zhuanlan.zhihu.com/p/22731540?refer=json-tutorial
	unicode的一个例子
	待解析的字符串 """\u20AC"""
	那么按照16进制的规则算出无符号数字的大小，然后按照编码规则，判断为n个字符，写入到按字节写入

*/
static const char* lept_parse_hex4(const char* p, unsigned* u) {
	// 将p开始连续的四个字符解析出来，放到无符号数字U中
	int i;
	*u = 0;
	for (i = 0; i < 4; i++) {
		char ch = *p++;
		*u <<= 4;
		if (ch >= '0' && ch <= '9')  *u |= ch - '0';
		else if (ch >= 'A' && ch <= 'F')  *u |= ch - ('A' - 10);
		else if (ch >= 'a' && ch <= 'f')  *u |= ch - ('a' - 10);
		else return NULL;
	}
	return p;
}

static void lept_encode_utf8(lept_context* c, unsigned u) {
	//将一个unsignedint转化，为标准的1-4个char数据，0xff是为了防止编译器的警告。
	if (u <= 0x7F)
		PUTC(c, u & 0xFF);
	else if (u <= 0x7FF) {
		PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
		PUTC(c, 0x80 | (u & 0x3F));
	}
	else if (u <= 0xFFFF) {
		PUTC(c, 0xE0 | ((u >> 12) & 0xFF));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}
	else {
		assert(u <= 0x10FFFF);
		PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
		PUTC(c, 0x80 | ((u >> 12) & 0x3F));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}
}

#define STRING_ERROR(ret) do { c->top = head; return ret; } while(0)
// 解析字符串，把结果写入str和len
// str指向 c->stack 中的元素，需要在 c->stack
static int lept_parse_string_raw(lept_context* c, char** str, size_t* plen) {
	size_t head = c->top, len;
	const char* p;
	unsigned u, u2;
	EXPECT(c, '\"'); //字符串应当是以“开头的
	p = c->json;
	for (;;) {
		char ch = *p++;
		switch (ch) {
		case '\"': // 表示字符串已经结束了
			len = c->top - head;
			*str = (char*)lept_context_pop(c, len);
			*plen = len;
			c->json = p;
			return LEPT_PARSE_OK;
		case '\\': // 接下来是转意字符
			switch (*p++) {
			case '\"': PUTC(c, '\"'); break;
			case '\\': PUTC(c, '\\'); break;
			case '/':  PUTC(c, '/');  break;
			case 'b':  PUTC(c, '\b'); break;
			case 'f':  PUTC(c, '\f'); break;
			case 'n':  PUTC(c, '\n'); break;
			case 'r':  PUTC(c, '\r'); break;
			case 't':  PUTC(c, '\t'); break;
			case 'u': {
				if (!(p = lept_parse_hex4(p, &u)))
					STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
				
				if (u >= 0xD800 && u <= 0xDBFF) { /* surrogate pair */
					if (*p++ != '\\')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (*p++ != 'u')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (!(p = lept_parse_hex4(p, &u2)))
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);
					if (u2 < 0xDC00 || u2 > 0xDFFF)
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;
				}
				lept_encode_utf8(c, u);
				break;
			}
			default:
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
			}
			break;
		case '\0':
			STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
		default:
			// TODO:\
			// 注意这里！如果是unicode就需要注意了
			if ((unsigned char)ch < 0x20) {
				c->top = head;
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
			}
			PUTC(c, ch);
		}
	}
}

static int lept_parse_string(lept_context* c, lept_value* v) {
	int ret;
	char* s;
	size_t len;
	if ((ret = lept_parse_string_raw(c, &s, &len)) == LEPT_PARSE_OK)
		lept_set_string(v, s, len);
	return ret;
} // 解析字符串的函数结束


static void lept_parse_whitespace(lept_context* c) {
	const char *p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	c->json = p;
}

static int lept_parse_null(lept_context* c, lept_value* v) {
	EXPECT(c, 'n');
	if (c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l')
		return LEPT_PARSE_INVALID_VALUE;
	c->json += 3;
	v->type = LEPT_NULL;
	return LEPT_PARSE_OK;
}

static int lept_parse_false(lept_context* c, lept_value* v) {
	EXPECT(c, 'f'); // 这一步如果成功，指针是++的
	if (c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' || c->json[3] != 'e')
		return LEPT_PARSE_INVALID_VALUE;
	c->json += 4;
	v->type = LEPT_FALSE;
	return LEPT_PARSE_OK;
}

static int lept_parse_true(lept_context* c, lept_value* v) {
	EXPECT(c, 't'); // 这一步如果成功，指针是++的
	if (c->json[0] != 'r' || c->json[1] != 'u' || c->json[2] != 'e')
		return LEPT_PARSE_INVALID_VALUE;
	c->json += 3;
	v->type = LEPT_TRUE;
	return LEPT_PARSE_OK;
}

// 解析数字
static int lept_parse_number(lept_context* c, lept_value* v) {
	const char* p = c->json;
	if (*p == '-') p++;
	if (*p == '0')
		p++;
	else {
		if (!ISDIGIT1TO9(*p)) return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}
	// 检查完负号和小数点前面的
	if (*p == '.') {
		p++;
		if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}
	// 检查完小数点到e符号之间的
	if (*p == 'e' || *p == 'E') {
		p++;
		if (*p == '+' || *p == '-') p++;
		if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}
	errno = 0;
	v->u.n = strtod(c->json, NULL);
	if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
		return LEPT_PARSE_NUMBER_TOO_BIG;

	v->type = LEPT_NUMBER;
	c->json = p;
	return LEPT_PARSE_OK;

}

// 解析数组
static int lept_parse_value(lept_context* c, lept_value* v); // 前向声明
static int lept_parse_array(lept_context* c, lept_value* v) {
	int bad = 0;
	size_t size = 0;
	int ret;
	EXPECT(c, '[');
	lept_parse_whitespace(c);
	if (*c->json == ']') {
		c->json++;
		v->type = LEPT_ARRAY;
		v->u.a.size = 0;
		v->u.a.e = NULL;
		return LEPT_PARSE_OK;
	}

	for (;;) {
		lept_value e;
		lept_init(&e);
		if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK) {
			bad = 1;
			break;
		}
		
		memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
		size ++;
		lept_parse_whitespace(c);
		if (*c->json == ',') {
			c->json++;
			lept_parse_whitespace(c);
		} else if (*c->json == ']') {
			c->json ++;
			v->type = LEPT_ARRAY;
			v->u.a.size = size;
			size *= sizeof(lept_value);
			memcpy(v->u.a.e = (lept_value*)malloc(size), lept_context_pop(c, size), size);
			return LEPT_PARSE_OK;
		} else {
			bad = 1;
			ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
	}

	if (bad) { //返回错误之前，需要释放那些临时的堆栈中的值
		size_t i = 0;
		for (i = 0; i < size; i++) //将栈中的指针回退的同时，释放栈中的每个value结构体指针的内存
			lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
		return ret;
	}
}


// 解析对象
static int lept_parse_object(lept_context* c, lept_value* v) {
	size_t i, size;
	lept_member m;
	int ret;
	EXPECT(c, '{');
	lept_parse_whitespace(c);
	if (*c->json == '}') {
		c->json++;
		v->type = LEPT_OBJECT;
		v->u.o.m = 0;
		v->u.o.size = 0;
		return LEPT_PARSE_OK;
	}
	m.k = NULL;
	size = 0;
	for (;;) {
		char* str;
		lept_init(&m.v);
		// 解析成员的键-字符串
		if (*c->json != '"') {
			ret = LEPT_PARSE_MISS_KEY;
			break;
		}
		if ((ret = lept_parse_string_raw(c, &str, &m.klen)) != LEPT_PARSE_OK) break;
		memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen + 1);
		/* 解析空白 + 冒号 + 空白 */
		lept_parse_whitespace(c);
		if (*c->json != ':') {
			ret = LEPT_PARSE_MISS_COLON;
			break;
		}
		c->json++;
		lept_parse_whitespace(c);

		// 解析成员的值
		if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK) break;
		memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
		size++;
		m.k = NULL; // 因为资源已经被复制到栈上了，这里的m是一个临时成员，因此准备下一轮
		
		// parse 空格 [',' | '}'] 空格 
		lept_parse_whitespace(c);
		if (*c->json == ',') {
			c->json++;
			lept_parse_whitespace(c);
		} else if (*c->json == '}') {
			size_t s = sizeof(lept_member) * size;
			c->json++;
			v->type = LEPT_OBJECT;
			v->u.o.size = size;
			memcpy(v->u.o.m = (lept_member*)malloc(s), lept_context_pop(c, s), s);
			return LEPT_PARSE_OK;
		} else {
			ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
	}

	// 释放临时成员的值，value成员不是指针，不用释放
	free(m.k);
	// 因为栈上的资源已经都复制到了结果中，因此释放栈上的资源
	for (i = 0; i < size; i++) {
		lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
		free(m->k);
		lept_free(&m->v);
	}
	v->type = LEPT_NULL;
	return ret;
}


static int lept_parse_value(lept_context* c, lept_value* v) {
	switch (*c->json) {
	case 'n':  return lept_parse_null(c, v);
	case '[':  return lept_parse_array(c, v);
	case 't': return lept_parse_true(c, v);
	case 'f': return lept_parse_false(c, v);
	case '\"': return lept_parse_string(c, v);
	case '{': return lept_parse_object(c, v);
	case '\0': return LEPT_PARSE_EXPECT_VALUE;
	default:   return lept_parse_number(c, v);
	}
}

// json-text 入口 : ws + value + ws
int lept_parse(lept_value* v, const char* json) {
	lept_context c;
	assert(v != NULL);
	c.json = json;
	c.stack = NULL;        /* <- */
	c.size = c.top = 0;    /* <- */
	lept_init(v);

	lept_parse_whitespace(&c);

	int ret = lept_parse_value(&c, v);
	// 无论是不是成功解析，都要释放资源
	if (ret != LEPT_PARSE_OK) {
		free(c.stack);
		return ret;
	} else {
		lept_parse_whitespace(&c);
		if (c.json[0] == '\0') {
			free(c.stack);
			return LEPT_PARSE_OK;
		} else {
			free(c.stack);
			return LEPT_PARSE_ROOT_NOT_SINGULAR;
		}	
	}

}

// 获取类型的函数
lept_type lept_get_type(const lept_value* v) {
	assert(v != NULL);
	return v->type;
}


// 生成器相关
#define PUTS(c, s, len) memcpy(lept_context_push(c, len), s, len)

static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
	size_t i;
	assert(s != NULL);
	PUTC(c, '"');
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		switch (ch) {
		case '\"': PUTS(c, "\\\"", 2); break;
		case '\\': PUTS(c, "\\\\", 2); break;
		case '\b': PUTS(c, "\\b", 2); break;
		case '\f': PUTS(c, "\\f", 2); break;
		case '\n': PUTS(c, "\\n", 2); break;
		case '\r': PUTS(c, "\\r", 2); break;
		case '\t': PUTS(c, "\\t", 2); break;
		default:
			if (ch < 0x20) { // TODO:\如何处理Unicode编码
				char buffer[7];
				sprintf(buffer, "\\u%04X", ch);
				PUTS(c, buffer, 6);
			}
			else
				PUTC(c, s[i]);
		}
	}
	PUTC(c, '"');
}

static int lept_stringify_value(lept_context* c, const lept_value* v) {
	size_t i;
	int ret;
	switch (v->type) {
		case LEPT_NULL:   PUTS(c, "null", 4); break;
		case LEPT_FALSE:  PUTS(c, "false", 5); break;
		case LEPT_TRUE:   PUTS(c, "true", 4); break;
		/*case LEPT_NUMBER: {
			char* buffer = lept_context_push(c, 32);
			int length = sprintf(buffer, "%.17g", v->u.n);
			c->top -= 32 - length;
		}*/
		case LEPT_NUMBER:
			c->top -= 32 - sprintf(lept_context_push(c, 32), "%.17g", v->u.n);
			break;
		case LEPT_STRING: lept_stringify_string(c, v->u.s.s, v->u.s.len); break;
		case LEPT_ARRAY:
			PUTC(c, '[');
			for (i = 0; i < v->u.a.size; i++) {
				if (i > 0)
					PUTC(c, ',');
				lept_stringify_value(c, &v->u.a.e[i]);
			}
			PUTC(c, ']');
			break;
		case LEPT_OBJECT:
			PUTC(c, '{');
			for (i = 0; i < v->u.o.size; i++) {
				if (i > 0)
					PUTC(c, ',');
				lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);
				PUTC(c, ':');
				lept_stringify_value(c, &v->u.o.m[i].v);
			}
			PUTC(c, '}');
			break;
		
	}
	return LEPT_STRINGIFY_OK;
}
/*
 使用空间换取时间的优化，预先分配好内存。
static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
	static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	size_t i, size;
	char* head, *p;
	assert(s != NULL);
	p = head = lept_context_push(c, size = len * 6 + 2); // "\u00xx..."
	*p++ = '"';
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];
		switch (ch) {
			case '\"': *p++ = '\\'; *p++ = '\"'; break;
			case '\\': *p++ = '\\'; *p++ = '\\'; break;
			case '\b': *p++ = '\\'; *p++ = 'b';  break;
			case '\f': *p++ = '\\'; *p++ = 'f';  break;
			case '\n': *p++ = '\\'; *p++ = 'n';  break;
			case '\r': *p++ = '\\'; *p++ = 'r';  break;
			case '\t': *p++ = '\\'; *p++ = 't';  break;
			default:
			if (ch < 0x20) {
				*p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
				*p++ = hex_digits[ch >> 4];
				*p++ = hex_digits[ch & 15];
			}
			else
				*p++ = s[i];
		}
	}
	*p++ = '"';
	c->top -= size - (p - head);
}
*/

int lept_stringify(const lept_value* v, char** json, size_t* length) {
	lept_context c;
	int ret;
	assert(v != NULL);
	assert(json != NULL);
	c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;
	if ((ret = lept_stringify_value(&c, v)) != LEPT_STRINGIFY_OK) {
		free(c.stack);
		*json = NULL;
		return ret;
	}
	if (length)
		*length = c.top;
	PUTC(&c, '\0');
	*json = c.stack;
	return LEPT_STRINGIFY_OK;
}


