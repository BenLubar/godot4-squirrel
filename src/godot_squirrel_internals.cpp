#include "../squirrel/squirrel/sqpcheader.h"
#include "../squirrel/squirrel/sqvm.h"
#include "../squirrel/squirrel/sqstring.h"
#include "../squirrel/squirrel/sqtable.h"
#include "../squirrel/squirrel/sqarray.h"
#include "../squirrel/squirrel/sqfuncproto.h"
#include "../squirrel/squirrel/sqclosure.h"
#include "../squirrel/squirrel/squserdata.h"
#include "../squirrel/squirrel/sqcompiler.h"
#include "../squirrel/squirrel/sqfuncstate.h"
#include "../squirrel/squirrel/sqclass.h"

#include "godot_squirrel_internals.h"

SQInteger godot_squirrel_get_generator_state(const HSQOBJECT *obj) {
	if (!sq_isgenerator(*obj)) {
		return SQ_VMSTATE_IDLE;
	}

	switch (obj->_unVal.pGenerator->_state) {
	case SQGenerator::eRunning:
		return SQ_VMSTATE_RUNNING;
	case SQGenerator::eSuspended:
		return SQ_VMSTATE_SUSPENDED;
	case SQGenerator::eDead:
	default:
		return SQ_VMSTATE_IDLE;
	}
}

void godot_squirrel_push_call_closure(HSQUIRRELVM vm, int64_t level) {
	const SQVM::CallInfo &ci = vm->_callsstack[vm->_callsstacksize - level - 1];
	sq_pushobject(vm, ci._closure);
}
