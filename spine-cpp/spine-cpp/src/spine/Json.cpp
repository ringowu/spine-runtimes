/******************************************************************************
* Spine Runtimes Software License v2.5
*
* Copyright (c) 2013-2016, Esoteric Software
* All rights reserved.
*
* You are granted a perpetual, non-exclusive, non-sublicensable, and
* non-transferable license to use, install, execute, and perform the Spine
* Runtimes software and derivative works solely for personal or internal
* use. Without the written permission of Esoteric Software (see Section 2 of
* the Spine Software License Agreement), you may not (a) modify, translate,
* adapt, or develop new applications using the Spine Runtimes or otherwise
* create derivative works or improvements of the Spine Runtimes or (b) remove,
* delete, alter, or obscure any trademarks or any copyright, trademark, patent,
* or other intellectual property or proprietary rights notices on or in the
* Software, including any copy thereof. Redistributions in binary or source
* form must include this license and terms.
*
* THIS SOFTWARE IS PROVIDED BY ESOTERIC SOFTWARE "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
* EVENT SHALL ESOTERIC SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, BUSINESS INTERRUPTION, OR LOSS OF
* USE, DATA, OR PROFITS) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/* Json */
/* JSON parser in CPP, shamelessly ripped from json.c in the spine-c runtime */

#ifndef _DEFAULT_SOURCE
/* Bring strings.h definitions into string.h, where appropriate */
#define _DEFAULT_SOURCE
#endif

#ifndef _BSD_SOURCE
/* Bring strings.h definitions into string.h, where appropriate */
#define _BSD_SOURCE
#endif

#include <spine/Json.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h> /* strtod (C89), strtof (C99) */
#include <string.h> /* strcasecmp (4.4BSD - compatibility), _stricmp (_WIN32) */
#include <spine/Extension.h>
#include <new>

namespace Spine
{
    const int Json::JSON_FALSE = 0;
    const int Json::JSON_TRUE = 1;
    const int Json::JSON_NULL = 2;
    const int Json::JSON_NUMBER = 3;
    const int Json::JSON_STRING = 4;
    const int Json::JSON_ARRAY = 5;
    const int Json::JSON_OBJECT = 6;
    
    const char* Json::_error = NULL;
    
    Json* Json::getItem(Json *object, const char* string) {
        Json *c = object->_child;
        while (c && json_strcasecmp(c->_name, string)) {
            c = c->_next;
        }
        return c;
    }
    
    const char* Json::getString(Json *object, const char* name, const char* defaultValue) {
        object = getItem(object, name);
        if (object) {
            return object->_valueString;
        }
        
        return defaultValue;
    }
    
    float Json::getFloat(Json *value, const char* name, float defaultValue) {
        value = getItem(value, name);
        return value ? value->_valueFloat : defaultValue;
    }
    
    int Json::getInt(Json *value, const char* name, int defaultValue) {
        value = getItem(value, name);
        return value ? value->_valueInt : defaultValue;
    }
    
    const char* Json::getError() {
        return _error;
    }
    
    Json::Json(const char* value) :
    _next(NULL),
#if SPINE_JSON_HAVE_PREV
    _prev(NULL),
#endif
    _child(NULL),
    _type(0),
    _size(0),
    _valueString(NULL),
    _valueInt(0),
    _valueFloat(0),
    _name(NULL) {
        if (value) {
            value = parseValue(this, skip(value));
            
            assert(value);
        }
    }
    
    Json::~Json() {
        if (_child) {
            DESTROY(Json, _child);
        }
        
        if (_valueString) {
            FREE(_valueString);
        }
        
        if (_name) {
            FREE(_name);
        }
        
        if (_next) {
            DESTROY(Json, _next);
        }
    }
    
    const char* Json::skip(const char* inValue) {
        if (!inValue) {
            /* must propagate NULL since it's often called in skip(f(...)) form */
            return NULL;
        }
        
        while (*inValue && (unsigned char)*inValue <= 32) {
            inValue++;
        }
        
        return inValue;
    }
    
    const char* Json::parseValue(Json *item, const char* value) {
        /* Referenced by constructor, parseArray(), and parseObject(). */
        /* Always called with the result of skip(). */
#if SPINE_JSON_DEBUG /* Checked at entry to graph, constructor, and after every parse call. */
        if (!value) {
            /* Fail on null. */
            return NULL;
        }
#endif
        
        switch (*value) {
            case 'n': {
                if (!strncmp(value + 1, "ull", 3)) {
                    item->_type = JSON_NULL;
                    return value + 4;
                }
                break;
            }
            case 'f': {
                if (!strncmp(value + 1, "alse", 4)) {
                    item->_type = JSON_FALSE;
                    /* calloc prevents us needing item->_type = JSON_FALSE or valueInt = 0 here */
                    return value + 5;
                }
                break;
            }
            case 't': {
                if (!strncmp(value + 1, "rue", 3)) {
                    item->_type = JSON_TRUE;
                    item->_valueInt = 1;
                    return value + 4;
                }
                break;
            }
            case '\"':
                return parseString(item, value);
            case '[':
                return parseArray(item, value);
            case '{':
                return parseObject(item, value);
            case '-': /* fallthrough */
            case '0': /* fallthrough */
            case '1': /* fallthrough */
            case '2': /* fallthrough */
            case '3': /* fallthrough */
            case '4': /* fallthrough */
            case '5': /* fallthrough */
            case '6': /* fallthrough */
            case '7': /* fallthrough */
            case '8': /* fallthrough */
            case '9':
                return parseNumber(item, value);
            default:
                break;
        }
        
        _error = value;
        return NULL; /* failure. */
    }
    
    static const unsigned char firstByteMark[7] = {0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC};
    const char* Json::parseString(Json *item, const char* str) {
        const char* ptr = str + 1;
        char* ptr2;
        char* out;
        int len = 0;
        unsigned uc, uc2;
        if (*str != '\"') {
            /* TODO: don't need this check when called from parseValue, but do need from parseObject */
            _error = str;
            return 0;
        } /* not a string! */
        
        while (*ptr != '\"' && *ptr && ++len) {
            if (*ptr++ == '\\') {
                ptr++; /* Skip escaped quotes. */
            }
        }
        
        out = MALLOC(char, len + 1); /* The length needed for the string, roughly. */
        if (!out) {
            return 0;
        }
        
        ptr = str + 1;
        ptr2 = out;
        while (*ptr != '\"' && *ptr) {
            if (*ptr != '\\') {
                *ptr2++ = *ptr++;
            }
            else {
                ptr++;
                switch (*ptr) {
                    case 'b':
                        *ptr2++ = '\b';
                        break;
                    case 'f':
                        *ptr2++ = '\f';
                        break;
                    case 'n':
                        *ptr2++ = '\n';
                        break;
                    case 'r':
                        *ptr2++ = '\r';
                        break;
                    case 't':
                        *ptr2++ = '\t';
                        break;
                    case 'u': {
                        /* transcode utf16 to utf8. */
                        sscanf(ptr + 1, "%4x", &uc);
                        ptr += 4; /* get the unicode char. */
                        
                        if ((uc >= 0xDC00 && uc <= 0xDFFF) || uc == 0) {
                            break; /* check for invalid.    */
                        }
                        
                        /* TODO provide an option to ignore surrogates, use unicode replacement character? */
                        if (uc >= 0xD800 && uc <= 0xDBFF) /* UTF16 surrogate pairs.    */ {
                            if (ptr[1] != '\\' || ptr[2] != 'u') {
                                break; /* missing second-half of surrogate.    */
                            }
                            sscanf(ptr + 3, "%4x", &uc2);
                            ptr += 6;
                            if (uc2 < 0xDC00 || uc2 > 0xDFFF) {
                                break; /* invalid second-half of surrogate.    */
                            }
                            uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                        }
                        
                        len = 4;
                        if (uc < 0x80) {
                            len = 1;
                        }
                        else if (uc < 0x800) {
                            len = 2;
                        }
                        else if (uc < 0x10000) {
                            len = 3;
                        }
                        ptr2 += len;
                        
                        switch (len) {
                            case 4:
                                *--ptr2 = ((uc | 0x80) & 0xBF);
                                uc >>= 6;
                                /* fallthrough */
                            case 3:
                                *--ptr2 = ((uc | 0x80) & 0xBF);
                                uc >>= 6;
                                /* fallthrough */
                            case 2:
                                *--ptr2 = ((uc | 0x80) & 0xBF);
                                uc >>= 6;
                                /* fallthrough */
                            case 1:
                                *--ptr2 = (uc | firstByteMark[len]);
                        }
                        ptr2 += len;
                        break;
                    }
                    default:
                        *ptr2++ = *ptr;
                        break;
                }
                ptr++;
            }
        }
        
        *ptr2 = NULL;
        
        if (*ptr == '\"') {
            ptr++; /* TODO error handling if not \" or \0 ? */
        }
        
        item->_valueString = out;
        item->_type = JSON_STRING;
        
        return ptr;
    }
    
    const char* Json::parseNumber(Json *item, const char* num) {
        double result = 0.0;
        int negative = 0;
        char* ptr = (char*)num;
        
        if (*ptr == '-') {
            negative = -1;
            ++ptr;
        }
        
        while (*ptr >= '0' && *ptr <= '9') {
            result = result * 10.0 + (*ptr - '0');
            ++ptr;
        }
        
        if (*ptr == '.') {
            double fraction = 0.0;
            int n = 0;
            ++ptr;
            
            while (*ptr >= '0' && *ptr <= '9') {
                fraction = (fraction * 10.0) + (*ptr - '0');
                ++ptr;
                ++n;
            }
            result += fraction / pow(10.0, n);
        }
        
        if (negative) {
            result = -result;
        }
        
        if (*ptr == 'e' || *ptr == 'E') {
            double exponent = 0;
            int expNegative = 0;
            int n = 0;
            ++ptr;
            
            if (*ptr == '-') {
                expNegative = -1;
                ++ptr;
            }
            else if (*ptr == '+') {
                ++ptr;
            }
            
            while (*ptr >= '0' && *ptr <= '9') {
                exponent = (exponent * 10.0) + (*ptr - '0');
                ++ptr;
                ++n;
            }
            
            if (expNegative) {
                result = result / pow(10, exponent);
            }
            else {
                result = result * pow(10, exponent);
            }
        }
        
        if (ptr != num) {
            /* Parse success, number found. */
            item->_valueFloat = result;
            item->_valueInt = static_cast<int>(result);
            item->_type = JSON_NUMBER;
            return ptr;
        }
        else {
            /* Parse failure, _error is set. */
            _error = num;
            return NULL;
        }
    }
    
    const char* Json::parseArray(Json *item, const char* value) {
        Json *child;
        
#if SPINE_JSON_DEBUG /* unnecessary, only callsite (parse_value) verifies this */
        if (*value != '[') {
            ep = value;
            return 0;
        } /* not an array! */
#endif
        
        item->_type = JSON_ARRAY;
        value = skip(value + 1);
        if (*value == ']') {
            return value + 1; /* empty array. */
        }
        
        item->_child = child = NEW(Json);
        new (item->_child) Json(NULL);
        if (!item->_child) {
            return NULL; /* memory fail */
        }
        
        value = skip(parseValue(child, skip(value))); /* skip any spacing, get the value. */
        
        if (!value) {
            return NULL;
        }
        
        item->_size = 1;
        
        while (*value == ',') {
            Json *new_item = NEW(Json);
            new (new_item) Json(NULL);
            if (!new_item) {
                return NULL; /* memory fail */
            }
            child->_next = new_item;
#if SPINE_JSON_HAVE_PREV
            new_item->prev = child;
#endif
            child = new_item;
            value = skip(parseValue(child, skip(value + 1)));
            if (!value) {
                return NULL; /* parse fail */
            }
            item->_size++;
        }
        
        if (*value == ']') {
            return value + 1; /* end of array */
        }
        
        _error = value;
        
        return NULL; /* malformed. */
    }
    
    /* Build an object from the text. */
    const char* Json::parseObject(Json *item, const char* value) {
        Json *child;
        
#if SPINE_JSON_DEBUG /* unnecessary, only callsite (parse_value) verifies this */
        if (*value != '{') {
            ep = value;
            return 0;
        } /* not an object! */
#endif
        
        item->_type = JSON_OBJECT;
        value = skip(value + 1);
        if (*value == '}') {
            return value + 1; /* empty array. */
        }
        
        item->_child = child = NEW(Json);
        new (item->_child) Json(NULL);
        if (!item->_child) {
            return NULL;
        }
        value = skip(parseString(child, skip(value)));
        if (!value) {
            return NULL;
        }
        child->_name = child->_valueString;
        child->_valueString = 0;
        if (*value != ':') {
            _error = value;
            return NULL;
        } /* fail! */
        
        value = skip(parseValue(child, skip(value + 1))); /* skip any spacing, get the value. */
        if (!value) {
            return NULL;
        }
        
        item->_size = 1;
        
        while (*value == ',') {
            Json *new_item = NEW(Json);
            new (new_item) Json(NULL);
            if (!new_item) {
                return NULL; /* memory fail */
            }
            child->_next = new_item;
#if SPINE_JSON_HAVE_PREV
            new_item->prev = child;
#endif
            child = new_item;
            value = skip(parseString(child, skip(value + 1)));
            if (!value) {
                return NULL;
            }
            child->_name = child->_valueString;
            child->_valueString = 0;
            if (*value != ':') {
                _error = value;
                return NULL;
            } /* fail! */
            
            value = skip(parseValue(child, skip(value + 1))); /* skip any spacing, get the value. */
            if (!value) {
                return NULL;
            }
            item->_size++;
        }
        
        if (*value == '}') {
            return value + 1; /* end of array */
        }
        
        _error = value;
        
        return NULL; /* malformed. */
    }
    
    int Json::json_strcasecmp(const char* s1, const char* s2) {
        /* TODO we may be able to elide these NULL checks if we can prove
         * the graph and input (only callsite is Json_getItem) should not have NULLs
         */
        if (s1 && s2) {
#if defined(_WIN32)
            return _stricmp(s1, s2);
#else
            return strcasecmp( s1, s2 );
#endif
        }
        else {
            if (s1 < s2) {
                return -1; /* s1 is null, s2 is not */
            }
            else if (s1 == s2) {
                return 0; /* both are null */
            }
            else {
                return 1; /* s2 is nul    s1 is not */
            }
        }
    }
}
