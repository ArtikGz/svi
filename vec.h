#ifndef __H_VEC
#define __H_VEC
#include <assert.h>
#define DEFINE_VEC(name, type) \
    typedef struct {\
        type* elements;\
        size_t count;\
        size_t capacity;\
    } name\

#define VEC_RESIZE(array) do{\
        if (array.count >= array.capacity) {\
            if (array.capacity == 0) array.capacity = 16;\
            array.capacity *= 2;\
            array.elements = realloc(array.elements, array.capacity*sizeof(*array.elements));\
        }\
    } while(0)

#define VEC_APPEND(array, element) do {\
        VEC_RESIZE(array);\
        array.elements[array.count++] = element;\
    } while(0)

#define VEC_INSERT(array, index, element) do {\
        VEC_RESIZE(array);\
        memmove(array.elements + index + 1, array.elements + index, array.count - index);\
        array.elements[index] = element;\
        array.count += 1;\
    } while(0)

#define VEC_REMOVE(array, index) do {\
        memmove(array.elements + index, array.elements + index + 1, array.count - index - 1);\
        array.count -= 1;\
    } while(0)
#endif
