#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <string.h>

#define MAX_DIMS 8

typedef struct
{
	char type;
	
	int ndims;
	int dims[MAX_DIMS];
	int datasize;
	
	void *data;
} luaA_Array;

typedef union
{
	char c;
	unsigned char C;
	short s;
	unsigned short S;
	long l;
	unsigned long L;
	float f;
	double d;
	lua_Number n;
	int b, B;
} luaA_ArrayElement;

/* Macro to simplify working with all of the different array types */

#define TYPESWITCH(submacro) case 'c': submacro(char, c); break; \
							 case 'C': submacro(unsigned char, C); break; \
							 case 's': submacro(short, s); break; \
							 case 'S': submacro(unsigned short, S); break; \
							 case 'l': submacro(long, l); break; \
							 case 'L': submacro(unsigned long, L); break; \
							 case 'f': submacro(float, f); break; \
							 case 'd': submacro(double, d); break; \
							 case 'n': submacro(lua_Number, n); break;

/* Convenient functions for working with arrays */

static void check_type_compatible(lua_State *L, luaA_Array *a1, luaA_Array *a2)
{
	if (a1->type!=a2->type)
		luaL_error(L, "arrays must be of same type");
}

static void check_size_compatible(lua_State *L, luaA_Array *a1, luaA_Array *a2)
{
	if (a1->ndims!=a2->ndims)
		luaL_error(L, "arrays have different number of dimensions");
	int i;
	for (i=0; i<a1->ndims; i++)
	{
		if (a1->dims[i]!=a2->dims[i])
			luaL_error(L, "array dimension #%d is of different size", i+1);
	}
}

static int calculate_index(luaA_Array *a, int *coords)
{
	int index = 0, size = 1, i;
	for (i=0; i<a->ndims; i++)
	{
		index += coords[i]*size;
		size*=a->dims[i];
	}
	
	return index;
}

static int calculate_total_size(luaA_Array *a)
{
	int d, total=1;
	for (d=0; d<a->ndims; d++)
	{
		total*=a->dims[d];
	}
	return total;
}

static void extract_element(luaA_Array *a, int index, luaA_ArrayElement *dest)
{
	switch(a->type)
	{
#define EXTRACT(type,umember) dest->umember = ((type*)a->data)[index];
		TYPESWITCH(EXTRACT);
		case 'B': EXTRACT(char,B); break;
#undef EXTRACT
		case 'b': *(int*)dest = !!(((unsigned char*)a->data)[index/8]&(1<<(index%8))); break;
	}
}

static void insert_element(luaA_Array *a, int index, luaA_ArrayElement *src)
{
	switch(a->type)
	{
#define INSERT(type,umember) ((type*)a->data)[index] = src->umember;
		TYPESWITCH(INSERT);
		case 'B': INSERT(char,B); break;
#undef INSERT
		case 'b':
		{
			unsigned char byte = ((unsigned char*)a->data)[index/8];
			if (*(int*)src) byte|=(1<<(index%8));
			else byte&=~(1<<(index%8));
			((unsigned char*)a->data)[index/8] = byte;
			break;
		}
	}
}

static int type_size_in_bits(char type)
{
	switch(type)
	{
#define SIZE(type,umember) return sizeof(type)*8;
		TYPESWITCH(SIZE)
#undef SIZE
		case 'B': return 8;
		case 'b': return 1;
	}
	return 0; /* should never happen */
}

static luaA_ArrayElement luaA_checkarrayelement(lua_State *L, int narg, char type)
{
	luaA_ArrayElement e;
	switch(type)
	{
#define CHECK(type,umember) e.umember = luaL_checknumber(L, narg);
		TYPESWITCH(CHECK);
#undef CHECK
		case 'B': luaL_checktype(L, narg, LUA_TBOOLEAN); e.B = lua_toboolean(L, narg); break;
		case 'b': luaL_checktype(L, narg, LUA_TBOOLEAN); e.b = lua_toboolean(L, narg); break;
	}
	return e;
}

static void luaA_pusharrayelement(lua_State *L, luaA_ArrayElement e, char type)
{
	switch(type)
	{
#define PUSH(type,umember) lua_pushnumber(L, e.umember);
		TYPESWITCH(PUSH);
#undef PUSH
		case 'B': lua_pushboolean(L, e.B); break;
		case 'b': lua_pushboolean(L, e.b); break;
	}
}

static void luaA_checkcoords(lua_State *L, int startnarg, luaA_Array *array, int *coords)
{
	int i;
	for (i=0; i<array->ndims; i++)
	{
		int coord = luaL_checknumber(L, 2+i);
		if (coord<1 || coord>array->dims[i])
		{
			luaL_error(L, "array index #%d out of range", i+1);
		}
		coords[i] = coord-1;
	}
}

/* Interface to Lua */

static int l_array(lua_State *L)
{
	const char *typestr = luaL_checkstring(L, 1);
	if (strlen(typestr)!=1) luaL_error(L, "bad type character");
	char type = *typestr;
	switch(type)
	{
		case 'c': case 'C': case 's': case 'S': case 'l': case 'L':
		case 'f': case 'd': case 'n': case 'b': case 'B':
			break;
		default:
			luaL_error(L, "bad type character");
	}
	
	int ndims = lua_gettop(L)-1;
	if (ndims>MAX_DIMS) luaL_error(L, "array cannot have more than %d dimensions", MAX_DIMS);
	
	int dims[MAX_DIMS];
	int totalsize = 1;
	
	int i;
	for (i=0; i<ndims; i++)
	{
		int dim = luaL_checknumber(L, i+2);
		if (dim<=0) luaL_error(L, "negative size");
		dims[i] = dim;
		totalsize*=dim;
	}
	
	int typebits = type_size_in_bits(type);
	void *data = malloc(totalsize*typebits/8+1);
	if (!data) luaL_error(L, "cannot allocate memory for array");
	bzero(data, totalsize*typebits/8 + 1);
	
	luaA_Array *a = lua_newuserdata(L, sizeof(luaA_Array));
	a->type = type;
	a->ndims = ndims;
	memcpy(a->dims, dims, MAX_DIMS*sizeof(int));
	a->datasize = totalsize*typebits/8+1;
	a->data = data;
	
	luaL_getmetatable(L, "array");
	lua_setmetatable(L, -2);
	
	return 1;
}

static int lm_array_set(lua_State *L)
{
	luaA_Array *array = luaL_checkudata(L, 1, "array");
	if (lua_gettop(L)!=2+array->ndims) luaL_error(L, "wrong number of arguments");
	
	int coords[MAX_DIMS], index;
	luaA_checkcoords(L, 2, array, coords);
	index = calculate_index(array, coords);
	
	luaA_ArrayElement e;
	e = luaA_checkarrayelement(L, 2+array->ndims, array->type);
	
	insert_element(array, index, &e);
	
	return 0;
}

static int lm_array_get(lua_State *L)
{
	luaA_Array *array = luaL_checkudata(L, 1, "array");
	if (lua_gettop(L)!=1+array->ndims) luaL_error(L, "wrong number of arguments");
	
	int coords[MAX_DIMS], index;
	luaA_checkcoords(L, 2, array, coords);
	index = calculate_index(array, coords);
	
	luaA_ArrayElement e;
	extract_element(array, index, &e);
	luaA_pusharrayelement(L, e, array->type);
	
	return 1;
}

static int lm_array_clear(lua_State *L)
{
	luaA_Array *array = luaL_checkudata(L, 1, "array");
	
	if (lua_gettop(L)>1)
	{
		luaA_ArrayElement c = luaA_checkarrayelement(L, 2, array->type);
		
		int count = calculate_total_size(array);
		int i;
		for (i=0; i<count; i++)
		{
			insert_element(array, i, &c);
		}
	}
	else
	{
		bzero(array->data, array->datasize);
	}
	
	return 0;
}

static int lm_array_bor(lua_State *L)
{
	luaA_Array *a1 = luaL_checkudata(L, 1, "array");
	luaA_Array *a2 = luaL_checkudata(L, 2, "array");
	check_type_compatible(L, a1, a2);
	check_size_compatible(L, a1, a2);
	
	int j, k, *d1=a1->data, *d2=a2->data;
	for (j=0; j<a1->datasize/sizeof(int); j++) d1[j] |= d2[j];
	unsigned char *cd1 = a1->data, *cd2 = a2->data;
	for (k=0; k<a1->datasize%sizeof(int); k++) cd1[j*sizeof(int)+k] |= cd2[j*sizeof(int)+k];
	
	return 0;
}

static int lm_array_band(lua_State *L)
{
	luaA_Array *a1 = luaL_checkudata(L, 1, "array");
	luaA_Array *a2 = luaL_checkudata(L, 2, "array");
	check_type_compatible(L, a1, a2);
	check_size_compatible(L, a1, a2);
	
	int j, k, *d1=a1->data, *d2=a2->data;
	for (j=0; j<a1->datasize/sizeof(int); j++) d1[j] &= d2[j];
	unsigned char *cd1 = a1->data, *cd2 = a2->data;
	for (k=0; k<a1->datasize%sizeof(int); k++) cd1[j*sizeof(int)+k] &= cd2[j*sizeof(int)+k];
	
	return 0;
}

static int lm_array_bxor(lua_State *L)
{
	luaA_Array *a1 = luaL_checkudata(L, 1, "array");
	luaA_Array *a2 = luaL_checkudata(L, 2, "array");
	check_type_compatible(L, a1, a2);
	check_size_compatible(L, a1, a2);
	
	int j, k, *d1=a1->data, *d2=a2->data;
	for (j=0; j<a1->datasize/sizeof(int); j++) d1[j] ^= d2[j];
	unsigned char *cd1 = a1->data, *cd2 = a2->data;
	for (k=0; k<a1->datasize%sizeof(int); k++) cd1[j*sizeof(int)+k] ^= cd2[j*sizeof(int)+k];
	
	return 0;
}

static int lm_array_bnot(lua_State *L)
{
	luaA_Array *a = luaL_checkudata(L, 1, "array");
	
	int j, k, *d=a->data;
	for (j=0; j<a->datasize/sizeof(int); j++) d[j] = ~d[j];
	unsigned char *cd = a->data;
	for (k=0; k<a->datasize%sizeof(int); k++) cd[j*sizeof(int)+k] = ~cd[j*sizeof(int)+k];
	
	return 0;
}

static int lm_array_add(lua_State *L)
{
	luaA_Array *a1 = luaL_checkudata(L, 1, "array");
	luaA_Array *a2 = luaL_checkudata(L, 2, "array");
	check_type_compatible(L, a1, a2);
	check_size_compatible(L, a1, a2);
	if (a1->type=='b' || a1->type=='B') luaL_error(L, "cannot add two arrays of boolean values");
	
	int size = calculate_total_size(a1);
	int i;
	for (i=0; i<size; i++)
	{
		switch(a1->type)
		{
#define ADD(type,umember) ((type*)a1->data)[i] += ((type*)a2->data)[i];
			TYPESWITCH(ADD);
#undef ADD
		}
	}
	
	return 0;
}

static int lm_array_multiply(lua_State *L)
{
	luaA_Array *a1 = luaL_checkudata(L, 1, "array");
	luaA_Array *a2 = luaL_checkudata(L, 2, "array");
	check_type_compatible(L, a1, a2);
	check_size_compatible(L, a1, a2);
	if (a1->type=='b' || a1->type=='B')
		luaL_error(L, "cannot multiply two arrays of boolean values");
	
	int size = calculate_total_size(a1);
	int i;
	for (i=0; i<size; i++)
	{
		switch(a1->type)
		{
#define MULTIPLY(type,umember) ((type*)a1->data)[i] *= ((type*)a2->data)[i];
			TYPESWITCH(MULTIPLY);
#undef MULTIPLY
		}
	}
	
	return 0;
}

static int lm_array_negate(lua_State *L)
{
	luaA_Array *a = luaL_checkudata(L, 1, "array");
	switch(a->type)
	{
		case 'b': case 'B':
			luaL_error(L, "cannot negate an array of boolean values"); break;
		case 'C': case 'S': case 'L':
			luaL_error(L, "cannot negate an array of unsigned values"); break;
	}
	
	int size = calculate_total_size(a);
	int i;
	for (i=0; i<size; i++)
	{
		switch(a->type)
		{
#define NEGATE(type,umember) ((type*)a->data)[i] = -((type*)a->data)[i];
			TYPESWITCH(NEGATE);
#undef NEGATE
		}
	}
	
	return 0;
}

static int lm_array_copy(lua_State *L)
{
	luaA_Array *a = luaL_checkudata(L, 1, "array");
	
	luaA_Array *a2 = lua_newuserdata(L, sizeof(luaA_Array));
	
	void *data2 = malloc(a->datasize);
	if (!data2) luaL_error(L, "cannot allocate memory for array");
	*a2 = *a;
	memcpy(data2,a->data,a->datasize);
	a2->data = data2;
	
	luaL_getmetatable(L, "array");
	lua_setmetatable(L, -2);
	
	return 1;
}

static int lm_array_shiftcopy(lua_State *L)
{
	luaA_Array *a = luaL_checkudata(L, 1, "array");
	
	int d, shifts[MAX_DIMS];
	for (d=0; d<a->ndims; d++) shifts[d] = luaL_checknumber(L, d+2);
	
	luaA_Array *a2 = lua_newuserdata(L, sizeof(luaA_Array));
	void *data2 = malloc(a->datasize);
	if (!data2) luaL_error(L, "cannot allocate memory for array.");
	*a2 = *a;
	a2->data = data2;
	
	int location[MAX_DIMS];
	bzero(location, sizeof(int)*MAX_DIMS);
	while (1)
	{
		int location2[MAX_DIMS];
		for (d=0; d<a->ndims; d++)
		{
			location2[d] = (location[d]+shifts[d]) % (a->dims[d]);
		}
		
		int index = calculate_index(a, location);
		int index2 = calculate_index(a2, location2);
		
		luaA_ArrayElement buffer;
		extract_element(a, index, &buffer);
		insert_element(a2, index2, &buffer);
		
		/* Iterate over every point in the array; we can't use nested for loops because there may
		be an arbitrary number of dimensions. */
		for (d=0; d<a->ndims; d++)
		{
			if (location[d]!=a->dims[d]-1)
			{
				location[d]++;
				bzero(location, sizeof(int)*d);
				break;
			}
		}
		if (d==a->ndims) break;
	}
	
	luaL_getmetatable(L, "array");
	lua_setmetatable(L, -2);
	
	return 1;
}

static int lm_array_getpointer(lua_State *L)
{
	luaA_Array *a = luaL_checkudata(L, 1, "array");
	lua_pushlightuserdata(L, a->data);
	return 1;
}

static int lmm_array_gc(lua_State *L)
{
	luaA_Array *array = luaL_checkudata(L, 1, "array");
	free(array->data);
	return 0;
}

static luaL_reg functions[] = {
	{"array", l_array},
	
	{NULL, NULL}
};

static luaL_reg methods[] = {
	{"set", lm_array_set},
	{"get", lm_array_get},
	
	{"clear", lm_array_clear},
	
	{"copy", lm_array_copy},
	
	{"bnot", lm_array_bnot},
	{"bor", lm_array_bor},
	{"band", lm_array_band},
	{"bxor", lm_array_bxor},
	
	{"add", lm_array_add},
	{"multiply", lm_array_multiply},
	{"negate", lm_array_negate},
	
	{"shiftcopy", lm_array_shiftcopy},
	
	{"getpointer", lm_array_getpointer},
	
	{NULL, NULL}
};

static luaL_reg metamethods[] = {
	{"__gc", lmm_array_gc},
	
	{NULL, NULL}
};

int luaopen_array(lua_State *L)
{
	int i;

	luaL_newmetatable(L, "array");
	for (i=0; metamethods[i].name; i++)
	{
		lua_pushcfunction(L, metamethods[i].func);
		lua_setfield(L, -2, metamethods[i].name);
	}
	
	lua_newtable(L);
	for (i=0; methods[i].name; i++)
	{
		lua_pushcfunction(L, methods[i].func);
		lua_setfield(L, -2, methods[i].name);
	}
	lua_setfield(L, -2, "__index");
	
	lua_pop(L, 1);
	
	luaL_register(L, "array", functions);
	
	return 1;
}