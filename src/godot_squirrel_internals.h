#pragma once

#include <squirrel.h>

SQUIRREL_API SQInteger godot_squirrel_get_generator_state(const HSQOBJECT *obj);
SQUIRREL_API void godot_squirrel_push_call_closure(HSQUIRRELVM vm, int64_t level);
