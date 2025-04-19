#include "../squirrel/squirrel/sqpcheader.h"

#include <godot_cpp/core/memory.hpp>

void *sq_vm_malloc(SQUnsignedInteger size) {
	return memalloc(size);
}

void *sq_vm_realloc(void *p, SQUnsignedInteger SQ_UNUSED_ARG(oldsize), SQUnsignedInteger size) {
	return memrealloc(p, size);
}

void sq_vm_free(void *p, SQUnsignedInteger SQ_UNUSED_ARG(size)) {
	memfree(p);
}
