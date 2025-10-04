#include "godot_squirrel_defs.h"

#include <godot_cpp/templates/hash_map.hpp>

#ifndef SQUIRREL_NO_RANDOMNUMBERGENERATOR
#include <godot_cpp/classes/random_number_generator.hpp>
#endif

#include <squirrel.h>
#include <sqstdaux.h>
#include <sqstdblob.h>
#include <sqstdmath.h>
#include <sqstdstring.h>
#include <stdarg.h>

#include "godot_squirrel_internals.h"

using namespace godot;

namespace {
	struct SquirrelVariantUserData {
		static const SQUserPointer type_tag;

		Variant variant;

		static SQInteger release_hook(SQUserPointer pointer, [[maybe_unused]] SQInteger size) {
			SquirrelVariantUserData *svud = reinterpret_cast<SquirrelVariantUserData *>(pointer);
			svud->~SquirrelVariantUserData();

			return 0;
		}

		// pushes a Variant to the top of the stack
		static void create(HSQUIRRELVM vm, const Variant &variant) {
			SQUserPointer pointer = sq_newuserdata(vm, sizeof(SquirrelVariantUserData));
			SquirrelVariantUserData *svud = reinterpret_cast<SquirrelVariantUserData *>(pointer);
			new (svud) SquirrelVariantUserData();
			sq_setreleasehook(vm, -1, &release_hook);
			sq_settypetag(vm, -1, type_tag);

			svud->variant = variant;
		}

		// retrieves a Variant from the stack
		static Variant get(HSQUIRRELVM vm, SQInteger index) {
			SQUserPointer pointer = nullptr, object_type_tag = nullptr;
			ERR_FAIL_COND_V(SQ_FAILED(sq_getuserdata(vm, index, &pointer, &object_type_tag)), Variant());
			ERR_FAIL_COND_V(object_type_tag != type_tag, Variant());

			const SquirrelVariantUserData *svud = reinterpret_cast<const SquirrelVariantUserData *>(pointer);
			return svud->variant;
		}
	};
	const SQUserPointer SquirrelVariantUserData::type_tag = const_cast<SQUserPointer *>(&SquirrelVariantUserData::type_tag);
}

struct SquirrelVariant::SquirrelVariantInternal {
	HSQOBJECT obj;

	SquirrelVariantInternal() {
		sq_resetobject(&obj);
	}

	void init(const Ref<SquirrelVM> &vm, SquirrelVariant *outer, const HSQOBJECT &init_obj);
};

struct SquirrelVMBase::SquirrelVMInternal {
	HSQUIRRELVM vm;

	struct SQObjectHasher {
		static _FORCE_INLINE_ uint32_t hash(const HSQOBJECT &obj) {
			uint32_t h = hash_murmur3_one_32(sq_type(obj));
			if constexpr (sizeof(SQRawObjectVal) > 4) {
				h = hash_murmur3_one_64(obj._unVal.raw, h);
			} else {
				h = hash_murmur3_one_32(obj._unVal.raw, h);
			}
			return h;
		}
	};
	struct SQObjectComparator {
		static _FORCE_INLINE_ bool compare(const HSQOBJECT &a, const HSQOBJECT &b) {
			return sq_type(a) == sq_type(b) && a._unVal.raw == b._unVal.raw;
		}
	};
	HashMap<HSQOBJECT, SquirrelVariant *, SQObjectHasher, SQObjectComparator> ref_objects;
	HashMap<Variant, Ref<SquirrelWeakRef>, VariantHasher, VariantComparator> memoized_variants;

	template<typename T>
	Ref<T> make_ref_object(const HSQOBJECT &obj) {
		DEV_ASSERT(!ref_objects.has(obj));

		Ref<T> ref;
		ref.instantiate();
		ref->_internal->init(reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(vm)), ref.ptr(), obj);
		return ref;
	}

	void clean_memoized_variants() {
		bool found = true;
		while (found) {
			found = false;
			for (auto it = memoized_variants.begin(); it != memoized_variants.end(); ++it) {
				const Ref<SquirrelUserData> ud = it->value->get_object();
				if (ud.is_null()) {
					memoized_variants.remove(it);
					found = true;
					break;
				}
			}
		}
	}

#ifndef SQUIRREL_NO_DEBUG
	static void debug_hook(HSQUIRRELVM v, SQInteger type, const SQChar *sourcename, SQInteger line, const SQChar *funcname) {
		SquirrelVM *vm = reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(v));
		Ref<SquirrelVMBase> vm_or_thread = vm;
		if (vm->_vm_internal->vm != v) {
			HSQOBJECT obj;
			obj._type = OT_THREAD;
			obj._unVal.pThread = v;

			const auto cached = vm->_vm_internal->ref_objects.find(obj);
			vm_or_thread = cached == vm->_vm_internal->ref_objects.end() ?
				vm->_vm_internal->make_ref_object<SquirrelThread>(obj) :
				Ref<SquirrelThread>(cached->value);
		}

		switch (type) {
		case 'c': // "call"
			vm->emit_signal("debug_call", vm_or_thread, sourcename, int64_t(line), funcname);
			break;
		case 'r': // "return"
			vm->emit_signal("debug_return", vm_or_thread, sourcename, int64_t(line), funcname);
			break;
		case 'l': // "line"
			vm->emit_signal("debug_line", vm_or_thread, sourcename, int64_t(line), funcname);
			break;
		default:
#ifdef DEV_ENABLED
			WARN_PRINT_ONCE(vformat("Unhandled Squirrel debug type %c", char(type)));
#endif
			break;
		}
	}
#endif
#ifndef SQUIRREL_NO_PRINT
	static String squirrel_vsprintf(const char *format, va_list args) {
		va_list args_copy;
		va_copy(args_copy, args);
		size_t length = vsnprintf(0, 0, format, args_copy);
		va_end(args_copy);

		PackedByteArray buffer;
		buffer.resize(length + 1);
		vsnprintf(reinterpret_cast<char *>(buffer.ptrw()), length + 1, format, args);

		buffer.resize(length);
		return buffer.get_string_from_utf8();
	}

	static void squirrel_print(HSQUIRRELVM v, const SQChar *format, ...) {
		va_list args;
		va_start(args, format);
		String message = squirrel_vsprintf(format, args);
		va_end(args);

		SquirrelVM *vm = reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(v));
		if (vm->_print_func.is_null()) {
			print_line(message);
		} else {
			vm->_print_func.call(message);
		}
	}

	static void squirrel_error(HSQUIRRELVM v, const SQChar *format, ...) {
		va_list args;
		va_start(args, format);
		String message = squirrel_vsprintf(format, args);
		va_end(args);

		SquirrelVM *vm = reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(v));
		if (vm->_error_func.is_null()) {
			print_error(message);
		} else {
			vm->_error_func.call(message);
		}
	}
#endif
	static SQInteger squirrel_callable_wrapper(HSQUIRRELVM vm);
};

void SquirrelVariant::SquirrelVariantInternal::init(const Ref<SquirrelVM> &vm, SquirrelVariant *outer, const HSQOBJECT &init_obj) {
	outer->_vm = vm;
	obj = init_obj;
	sq_addref(vm->_vm_internal->vm, &obj);
}

void SquirrelStackInfo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source"), &SquirrelStackInfo::get_source);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_source");
	ClassDB::bind_method(D_METHOD("get_function_name"), &SquirrelStackInfo::get_function_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "function_name", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_function_name");
	ClassDB::bind_method(D_METHOD("get_line_number"), &SquirrelStackInfo::get_line_number);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "line_number", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_line_number");
	ClassDB::bind_method(D_METHOD("get_function"), &SquirrelStackInfo::get_function);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "function", PROPERTY_HINT_RESOURCE_TYPE, SquirrelAnyFunction::get_class_static(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_function");
	ClassDB::bind_method(D_METHOD("get_local_variable_names"), &SquirrelStackInfo::get_local_variable_names);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "local_variable_names", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_local_variable_names");
	ClassDB::bind_method(D_METHOD("get_local_variable_values"), &SquirrelStackInfo::get_local_variable_values);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "local_variable_values", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), StringName(), "get_local_variable_values");
}

String SquirrelStackInfo::get_source() const { return _source; }
String SquirrelStackInfo::get_function_name() const { return _function_name; }
int64_t SquirrelStackInfo::get_line_number() const { return _line_number; }
Ref<SquirrelAnyFunction> SquirrelStackInfo::get_function() const { return _function; }
PackedStringArray SquirrelStackInfo::get_local_variable_names() const { return _local_variable_names; }
Array SquirrelStackInfo::get_local_variable_values() const { return _local_variable_values; }

void SquirrelVMBase::_bind_methods() {
	BIND_ENUM_CONSTANT(IDLE);
	static_assert(IDLE == SQ_VMSTATE_IDLE);
	BIND_ENUM_CONSTANT(RUNNING);
	static_assert(RUNNING == SQ_VMSTATE_RUNNING);
	BIND_ENUM_CONSTANT(SUSPENDED);
	static_assert(SUSPENDED == SQ_VMSTATE_SUSPENDED);

	ClassDB::bind_method(D_METHOD("import", "script", "debug_file_name"), &SquirrelVMBase::import, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("import_script", "script", "debug_file_name"), &SquirrelVMBase::import_script, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("import_blob"), &SquirrelVMBase::import_blob);
	ClassDB::bind_method(D_METHOD("import_math"), &SquirrelVMBase::import_math);
	ClassDB::bind_method(D_METHOD("import_string"), &SquirrelVMBase::import_string);
	ClassDB::bind_vararg_method(METHOD_FLAG_VARARG, "call_function", &SquirrelVMBase::call_function, MethodInfo("call_function", PropertyInfo(Variant::OBJECT, "func", PROPERTY_HINT_RESOURCE_TYPE, SquirrelCallable::get_class_static()), PropertyInfo(Variant::NIL, "this", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT)));
	ClassDB::bind_method(D_METHOD("apply_function", "func", "this", "args"), &SquirrelVMBase::apply_function);
	ClassDB::bind_method(D_METHOD("apply_function_catch", "func", "this", "args"), &SquirrelVMBase::apply_function_catch);
	ClassDB::bind_method(D_METHOD("resume_generator", "generator"), &SquirrelVMBase::resume_generator);

	ClassDB::bind_method(D_METHOD("get_stack", "index"), &SquirrelVMBase::get_stack);
	ClassDB::bind_method(D_METHOD("get_stack_top"), &SquirrelVMBase::get_stack_top);
	ClassDB::bind_method(D_METHOD("push_stack", "value"), &SquirrelVMBase::push_stack);
	ClassDB::bind_method(D_METHOD("pop_stack", "count"), &SquirrelVMBase::pop_stack, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("remove_stack", "index"), &SquirrelVMBase::remove_stack);

	ClassDB::bind_method(D_METHOD("get_state"), &SquirrelVMBase::get_state);
	ClassDB::bind_method(D_METHOD("is_suspended"), &SquirrelVMBase::is_suspended);
	ClassDB::bind_method(D_METHOD("wake_up", "value"), &SquirrelVMBase::wake_up, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("wake_up_throw", "exception"), &SquirrelVMBase::wake_up_throw);

	ClassDB::bind_method(D_METHOD("create_blob", "data"), &SquirrelVMBase::create_blob);
	ClassDB::bind_method(D_METHOD("create_table"), &SquirrelVMBase::create_table);
	ClassDB::bind_method(D_METHOD("create_table_with_initial_capacity", "size"), &SquirrelVMBase::create_table_with_initial_capacity);
	ClassDB::bind_method(D_METHOD("create_array", "size"), &SquirrelVMBase::create_array);
	ClassDB::bind_method(D_METHOD("create_thread"), &SquirrelVMBase::create_thread);
	ClassDB::bind_method(D_METHOD("wrap_variant", "value"), &SquirrelVMBase::wrap_variant);
	ClassDB::bind_method(D_METHOD("intern_variant", "value"), &SquirrelVMBase::intern_variant);
	ClassDB::bind_method(D_METHOD("wrap_callable", "callable", "varargs"), &SquirrelVMBase::wrap_callable, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("convert_variant", "value", "wrap_unhandled_values"), &SquirrelVMBase::convert_variant, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("collect_garbage"), &SquirrelVMBase::collect_garbage);
	ClassDB::bind_method(D_METHOD("resurrect_unreachable"), &SquirrelVMBase::resurrect_unreachable);

	ClassDB::bind_method(D_METHOD("set_error_handler", "func"), &SquirrelVMBase::set_error_handler);
	ClassDB::bind_method(D_METHOD("set_error_handler_default"), &SquirrelVMBase::set_error_handler_default);
	ClassDB::bind_method(D_METHOD("set_handle_caught_errors", "enable"), &SquirrelVMBase::set_handle_caught_errors);
	ClassDB::bind_method(D_METHOD("get_last_error"), &SquirrelVMBase::get_last_error);
	ClassDB::bind_method(D_METHOD("reset_last_error"), &SquirrelVMBase::reset_last_error);

	ClassDB::bind_method(D_METHOD("set_root_table", "root_table"), &SquirrelVMBase::set_root_table);
	ClassDB::bind_method(D_METHOD("get_root_table"), &SquirrelVMBase::get_root_table);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "root_table", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_NONE), "set_root_table", "get_root_table");

	ClassDB::bind_method(D_METHOD("set_const_table", "root_table"), &SquirrelVMBase::set_const_table);
	ClassDB::bind_method(D_METHOD("get_const_table"), &SquirrelVMBase::get_const_table);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "const_table", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_NONE), "set_const_table", "get_const_table");

	ClassDB::bind_method(D_METHOD("get_registry_table"), &SquirrelVMBase::get_registry_table);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "registry_table", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_registry_table");

	ClassDB::bind_method(D_METHOD("get_stack_info", "level"), &SquirrelVMBase::get_stack_info);
	ClassDB::bind_method(D_METHOD("print_call_stack"), &SquirrelVMBase::print_call_stack);
}

SquirrelVMBase::SquirrelVMBase(bool create) {
	if (create) {
		_vm_internal = memnew(SquirrelVMInternal);
		_vm_internal->vm = sq_open(SQUIRREL_INITIAL_STACK_SIZE);
		if (!_vm_internal->vm) {
			ERR_PRINT("Failed to create Squirrel VM: out of memory?");
			memdelete(_vm_internal);
			_vm_internal = nullptr;
			return;
		}

#ifndef SQUIRREL_NO_DEBUG
		sq_enabledebuginfo(_vm_internal->vm, SQTrue);

		// prevent scripts from messing with our debug settings
		sq_pushroottable(_vm_internal->vm);
		sq_pushstring(_vm_internal->vm, "setdebughook", -1);
		sq_pushstring(_vm_internal->vm, "enabledebuginfo", -1);
		sq_rawdeleteslot(_vm_internal->vm, -3, SQFalse);
		sq_rawdeleteslot(_vm_internal->vm, -2, SQFalse);
		sq_poptop(_vm_internal->vm);
#endif
#ifndef SQUIRREL_NO_PRINT
		sq_setprintfunc(_vm_internal->vm, &SquirrelVMInternal::squirrel_print, &SquirrelVMInternal::squirrel_error);
		sqstd_seterrorhandlers(_vm_internal->vm);
#endif

		sq_setsharedforeignptr(_vm_internal->vm, this);
	}
}

SquirrelVMBase::~SquirrelVMBase() {
	if (_vm_internal) {
		sq_close(_vm_internal->vm);
		memdelete(_vm_internal);
	}
}

#define GET_VM(...) \
	if (unlikely(!_vm_internal && sq_isnull(_internal->obj))) { return __VA_ARGS__; } \
	DEV_ASSERT(_vm_internal || sq_isthread(_internal->obj)); \
	HSQUIRRELVM vm = likely(_vm_internal) ? _vm_internal->vm : _internal->obj._unVal.pThread; \
	ERR_FAIL_NULL_V(vm, __VA_ARGS__)

#define GET_OUTER_VM() \
	DEV_ASSERT(vm); \
	SquirrelVM *outer_vm = reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(vm)); \
	DEV_ASSERT(outer_vm); \
	DEV_ASSERT(outer_vm->_vm_internal)

namespace {
	struct SquirrelByteCodeReader {
		PackedByteArray bytes;
		SQInteger offset = 0;

		static SQInteger read(SQUserPointer p_reader, SQUserPointer p_data, SQInteger p_count) {
			SquirrelByteCodeReader *reader = reinterpret_cast<SquirrelByteCodeReader *>(p_reader);

			const SQInteger count = MIN(reader->bytes.size() - reader->offset, p_count);
			DEV_ASSERT(count >= 0);

			memcpy(p_data, reader->bytes.ptr() + reader->offset, count);
			reader->offset += count;

			return count;
		}
	};
}

Ref<SquirrelFunction> SquirrelVMBase::import(const Ref<SquirrelScript> &p_script, const String &p_debug_file_name) {
	ERR_FAIL_COND_V(p_script.is_null(), Ref<SquirrelFunction>());
	GET_VM(Ref<SquirrelFunction>());
	GET_OUTER_VM();

	if (unlikely(p_script->get_bytecode().is_empty())) {
		const String file_name = p_debug_file_name.is_empty() ? p_script->get_name() : p_debug_file_name;
		const CharString source_bytes = p_script->get_source().utf8();
		ERR_FAIL_COND_V_MSG(SQ_FAILED(sq_compilebuffer(vm, source_bytes, source_bytes.length(), file_name.utf8(), SQTrue)), Ref<SquirrelFunction>(), "Squirrel script parsing failed");
	} else {
		SquirrelByteCodeReader reader{p_script->get_bytecode()};
		ERR_FAIL_COND_V_MSG(SQ_FAILED(sq_readclosure(vm, &SquirrelByteCodeReader::read, &reader)), Ref<SquirrelFunction>(), "Squirrel bytecode parsing failed");
	}

	HSQOBJECT obj;
	sq_resetobject(&obj);
	CRASH_COND_MSG(SQ_FAILED(sq_getstackobj(vm, -1, &obj)), "Failed to get closure from stack");
	DEV_ASSERT(sq_isclosure(obj));

	const Ref<SquirrelFunction> func = outer_vm->_vm_internal->make_ref_object<SquirrelFunction>(obj);
	sq_poptop(vm);

	return func;
}

Ref<SquirrelFunction> SquirrelVMBase::import_script(const String &p_script, const String &p_debug_file_name) {
	Ref<SquirrelScript> script;
	script.instantiate();
	script->set_source(p_script);

	return import(script, p_debug_file_name);
}

void SquirrelVMBase::import_blob() {
	GET_VM();

	sq_pushroottable(vm);
	sqstd_register_bloblib(vm);
	sq_poptop(vm);
}

#ifndef SQUIRREL_NO_RANDOMNUMBERGENERATOR
static SQInteger squirrel_math_rand(HSQUIRRELVM vm) {
	const Ref<RandomNumberGenerator> rng = SquirrelVariantUserData::get(vm, -1);
	CRASH_COND(rng.is_null());

	sq_pushinteger(vm, rng->randi());

	return 1;
}

static SQInteger squirrel_math_srand(HSQUIRRELVM vm) {
	const Ref<RandomNumberGenerator> rng = SquirrelVariantUserData::get(vm, -1);
	CRASH_COND(rng.is_null());

	SQInteger seed = 0;
	if (unlikely(SQ_FAILED(sq_getinteger(vm, -2, &seed)))) {
		return sq_throwerror(vm, "seed must be an integer");
	}

	rng->set_seed(seed);

	return 0;
}
#endif

void SquirrelVMBase::import_math() {
	GET_VM();

	sq_pushroottable(vm);
	sqstd_register_mathlib(vm);

#ifndef SQUIRREL_NO_RANDOMNUMBERGENERATOR
	sq_pushstring(vm, "RAND_MAX", -1);
	sq_pushinteger(vm, UINT32_MAX);
	sq_newslot(vm, -3, SQFalse);

	Ref<RandomNumberGenerator> rng;
	rng.instantiate();
#ifndef SQUIRREL_RANDOMNUMBERGENERATOR_RANDOMSEED
	rng->set_seed(0);
#endif
	Ref<SquirrelUserData> rng_ud = wrap_variant(rng);

	sq_pushstring(vm, "rand", -1);
	sq_pushobject(vm, rng_ud->_internal->obj);
	sq_newclosure(vm, &squirrel_math_rand, 1);
	sq_setparamscheck(vm, 1, nullptr);
	sq_setnativeclosurename(vm, -1, "rand");
	sq_newslot(vm, -3, SQFalse);

	sq_pushstring(vm, "srand", -1);
	sq_pushobject(vm, rng_ud->_internal->obj);
	sq_newclosure(vm, &squirrel_math_srand, 1);
	sq_setparamscheck(vm, 2, ".n");
	sq_setnativeclosurename(vm, -1, "srand");
	sq_newslot(vm, -3, SQFalse);
#endif

	sq_poptop(vm);
}

void SquirrelVMBase::import_string() {
	GET_VM();

	sq_pushroottable(vm);
	sqstd_register_stringlib(vm);
	sq_poptop(vm);
}

Variant SquirrelVMBase::call_function(const Variant **p_args, GDExtensionInt p_arg_count, GDExtensionCallError &r_error) {
	DEV_ASSERT(p_arg_count >= 2);

	const Ref<SquirrelCallable> func = *p_args[0];
	ERR_FAIL_COND_V(func.is_null(), Variant());
	ERR_FAIL_COND_V(!func->is_owned_by(this), Variant());

	GET_VM(Variant());
	GET_OUTER_VM();

	sq_pushobject(vm, func->_internal->obj);
	for (int arg = 1; arg < p_arg_count; arg++) {
		if (unlikely(!push_stack(*p_args[arg]))) {
			sq_pop(vm, arg);
			r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
			r_error.argument = arg;
			r_error.expected = GDEXTENSION_VARIANT_TYPE_OBJECT;
			outer_vm->_vm_internal->clean_memoized_variants();
			ERR_FAIL_V(Variant());
		}
	}

	if (unlikely(SQ_FAILED(sq_call(vm, p_arg_count - 1, SQTrue, SQTrue)))) {
		sq_poptop(vm);
		outer_vm->_vm_internal->clean_memoized_variants();
		ERR_FAIL_V(Variant());
	}

	const Variant result = get_stack(-1);
	sq_poptop(vm);

	if (sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
		sq_poptop(vm);
	}

	outer_vm->_vm_internal->clean_memoized_variants();

	return result;
}

Variant SquirrelVMBase::apply_function(const Ref<SquirrelCallable> &p_func, const Variant &p_this, const Array &p_args) {
	const Variant ret = apply_function_catch(p_func, p_this, p_args);

	const Ref<SquirrelThrow> error = ret;
	ERR_FAIL_COND_V_MSG(error.is_valid(), Variant(), error->get_exception().stringify());

	return ret;
}

Variant SquirrelVMBase::apply_function_catch(const Ref<SquirrelCallable> &p_func, const Variant &p_this, const Array &p_args) {
	ERR_FAIL_COND_V(p_func.is_null(), SquirrelThrow::make("func is null"));
	ERR_FAIL_COND_V(!p_func->is_owned_by(this), SquirrelThrow::make("func is not owned by this VM"));

	GET_VM(SquirrelThrow::make("could not get VM"));
	GET_OUTER_VM();

	sq_pushobject(vm, p_func->_internal->obj);
	if (unlikely(!push_stack(p_this))) {
		sq_poptop(vm);
		outer_vm->_vm_internal->clean_memoized_variants();
		ERR_FAIL_V(SquirrelThrow::make("could not push arguments to stack"));
	}
	for (int64_t i = 0; i < p_args.size(); i++) {
		if (unlikely(!push_stack(p_args[i]))) {
			sq_pop(vm, i + 2);
			outer_vm->_vm_internal->clean_memoized_variants();
			ERR_FAIL_V(SquirrelThrow::make("could not push arguments to stack"));
		}
	}

	if (unlikely(SQ_FAILED(sq_call(vm, p_args.size() + 1, SQTrue, SQTrue)))) {
		sq_poptop(vm);
		outer_vm->_vm_internal->clean_memoized_variants();
		return SquirrelThrow::make(get_last_error());
	}

	const Variant result = get_stack(-1);
	sq_poptop(vm);

	if (sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
		sq_poptop(vm);
	}

	outer_vm->_vm_internal->clean_memoized_variants();

	return result;
}

Variant SquirrelVMBase::resume_generator(const Ref<SquirrelGenerator> &p_generator) {
	ERR_FAIL_COND_V(p_generator.is_null(), Variant());
	ERR_FAIL_COND_V(!p_generator->is_owned_by(this), Variant());

	GET_VM(Variant());

	sq_pushobject(vm, p_generator->_internal->obj);
	sq_resume(vm, SQTrue, SQTrue);

	const Variant result = get_stack(-1);
	sq_pop(vm, 2);

	return result;
}

Variant SquirrelVMBase::get_stack(int64_t p_index) const {
	GET_VM(Variant());
	GET_OUTER_VM();

	HSQOBJECT obj{};
	sq_resetobject(&obj);
	ERR_FAIL_COND_V(SQ_FAILED(sq_getstackobj(vm, p_index, &obj)), Variant());

	const auto ref = outer_vm->_vm_internal->ref_objects.find(obj);
	if (ref != outer_vm->_vm_internal->ref_objects.end()) {
		return ref->value;
	}

	switch (sq_type(obj)) {
	case OT_NULL:
		return Variant();
	case OT_INTEGER:
		return static_cast<int64_t>(sq_objtointeger(&obj));
	case OT_FLOAT:
		return static_cast<double>(sq_objtofloat(&obj));
	case OT_BOOL:
		return sq_objtobool(&obj) != SQFalse;
	case OT_STRING:
	{
		const SQChar *string_chars = nullptr;
		SQInteger string_size = 0;
		ERR_FAIL_COND_V(SQ_FAILED(sq_getstringandsize(vm, p_index, &string_chars, &string_size)), String());
		return String::utf8(string_chars, string_size);
	}
	case OT_TABLE:
		return outer_vm->_vm_internal->make_ref_object<SquirrelTable>(obj);
	case OT_ARRAY:
		return outer_vm->_vm_internal->make_ref_object<SquirrelArray>(obj);
	case OT_USERDATA:
		return outer_vm->_vm_internal->make_ref_object<SquirrelUserData>(obj);
	case OT_CLOSURE:
		return outer_vm->_vm_internal->make_ref_object<SquirrelFunction>(obj);
	case OT_NATIVECLOSURE:
		return outer_vm->_vm_internal->make_ref_object<SquirrelNativeFunction>(obj);
	case OT_GENERATOR:
		return outer_vm->_vm_internal->make_ref_object<SquirrelGenerator>(obj);
	case OT_USERPOINTER:
		// while OT_USERPOINTER is technically valid here, we should never be
		// in a situation where get_stack_obj is called while one is present.
		CRASH_NOW_MSG("unexpected OT_USERPOINTER on stack");
	case OT_THREAD:
		return outer_vm->_vm_internal->make_ref_object<SquirrelThread>(obj);
	case OT_FUNCPROTO:
		// Squirrel internal type; should not be on stack
		CRASH_NOW_MSG("unexpected OT_FUNCPROTO on stack");
	case OT_CLASS:
		return outer_vm->_vm_internal->make_ref_object<SquirrelClass>(obj);
	case OT_INSTANCE:
		return outer_vm->_vm_internal->make_ref_object<SquirrelInstance>(obj);
	case OT_WEAKREF:
		return outer_vm->_vm_internal->make_ref_object<SquirrelWeakRef>(obj);
	case OT_OUTER:
		// Squirrel internal type; should not be on stack
		CRASH_NOW_MSG("unexpected OT_OUTER on stack");
	}

	ERR_FAIL_V_MSG(Variant(), vformat("Squirrel: unhandled type %08x for object on stack", sq_type(obj))); // should be unreachable
}

int64_t SquirrelVMBase::get_stack_top() const {
	GET_VM(0);

	return sq_gettop(vm);
}

bool SquirrelVMBase::push_stack(const godot::Variant &p_value) {
	GET_VM(false);

	switch (p_value.get_type()) {
	case Variant::NIL:
		sq_pushnull(vm);
		return true;
	case Variant::INT:
		sq_pushinteger(vm, static_cast<SQInteger>(p_value.operator int64_t()));
		return true;
	case Variant::FLOAT:
		sq_pushfloat(vm, static_cast<SQFloat>(p_value.operator double()));
		return true;
	case Variant::BOOL:
		sq_pushbool(vm, p_value.operator bool() ? SQTrue : SQFalse);
		return true;
	case Variant::STRING:
	case Variant::STRING_NAME: // support automatically converting StringName to String for convenience
	{
		CharString string_bytes = p_value.operator String().utf8();
		sq_pushstring(vm, string_bytes, string_bytes.length());
		return true;
	}
	case Variant::OBJECT:
	{
		Object *object = p_value;
		if (!object) {
			sq_pushnull(vm);
			return true;
		}

		Ref<SquirrelVariant> sqvar = Object::cast_to<SquirrelVariant>(object);
		ERR_FAIL_COND_V_MSG(sqvar.is_null(), false, vformat("Cannot push object of type %s to the Squirrel stack. Use wrap_variant to pass an opaque object to Squirrel.", object->get_class()));
		ERR_FAIL_COND_V(!sqvar->is_owned_by(this), false);

		DEV_ASSERT(!sq_isnull(sqvar->_internal->obj));

		sq_pushobject(vm, sqvar->_internal->obj);
		return true;
	}
	default:
		break;
	}

	ERR_FAIL_V_MSG(false, vformat("Cannot push value %s of type %s to the Squirrel stack.", p_value, p_value.get_type_name(p_value.get_type())));
}

void SquirrelVMBase::pop_stack(int64_t p_count) {
	GET_VM();

	ERR_FAIL_COND(p_count < 0);
	ERR_FAIL_COND(p_count > sq_gettop(vm));

	sq_pop(vm, p_count);
}

void SquirrelVMBase::remove_stack(int64_t p_index) {
	GET_VM();

	sq_remove(vm, p_index);
}

SquirrelVMBase::VMState SquirrelVMBase::get_state() const {
	GET_VM(IDLE);

	return static_cast<VMState>(sq_getvmstate(vm));
}

bool SquirrelVMBase::is_suspended() const {
	GET_VM(false);

	return sq_getvmstate(vm) == SQ_VMSTATE_SUSPENDED;
}

Variant SquirrelVMBase::wake_up(const Variant &p_value) {
	ERR_FAIL_COND_V(!is_suspended(), Variant());
	GET_VM(Variant());

	ERR_FAIL_COND_V(!push_stack(p_value), Variant());
	if (unlikely(SQ_FAILED(sq_wakeupvm(vm, SQTrue, SQTrue, SQTrue, SQFalse)))) {
		sq_poptop(vm);
		ERR_FAIL_V(Variant());
	}

	const Variant result = get_stack(-1);
	sq_poptop(vm);

	if (sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
		sq_poptop(vm);
	}

	return result;
}

Variant SquirrelVMBase::wake_up_throw(const Variant &p_exception) {
	ERR_FAIL_COND_V(!is_suspended(), Variant());
	GET_VM(Variant());

	ERR_FAIL_COND_V(!push_stack(p_exception), Variant());
	sq_throwobject(vm);
	if (unlikely(SQ_FAILED(sq_wakeupvm(vm, SQFalse, SQTrue, SQTrue, SQTrue)))) {
		sq_poptop(vm);
		ERR_FAIL_V(Variant());
	}

	const Variant result = get_stack(-1);
	sq_poptop(vm);

	if (sq_getvmstate(vm) != SQ_VMSTATE_SUSPENDED) {
		sq_poptop(vm);
	}

	return result;
}

Ref<SquirrelInstance> SquirrelVMBase::create_blob(const PackedByteArray &p_data) {
	GET_VM(Ref<SquirrelInstance>());

	SQUserPointer data = sqstd_createblob(vm, p_data.size());
	ERR_FAIL_NULL_V(data, Ref<SquirrelInstance>());
	memcpy(data, p_data.ptr(), p_data.size());

	const Ref<SquirrelInstance> inst = get_stack(-1);
	DEV_ASSERT(inst.is_valid());

	sq_poptop(vm);

	return inst;
}

Ref<SquirrelTable> SquirrelVMBase::create_table() {
	GET_VM(Ref<SquirrelTable>());

	sq_newtable(vm);
	const Ref<SquirrelTable> table = get_stack(-1);
	DEV_ASSERT(table.is_valid());

	sq_poptop(vm);

	return table;
}

Ref<SquirrelTable> SquirrelVMBase::create_table_with_initial_capacity(int64_t p_size) {
	GET_VM(Ref<SquirrelTable>());

	sq_newtableex(vm, p_size);
	const Ref<SquirrelTable> table = get_stack(-1);
	DEV_ASSERT(table.is_valid());

	sq_poptop(vm);

	return table;
}

Ref<SquirrelArray> SquirrelVMBase::create_array(int64_t p_size) {
	GET_VM(Ref<SquirrelArray>());

	sq_newarray(vm, p_size);
	const Ref<SquirrelArray> array = get_stack(-1);
	DEV_ASSERT(array.is_valid());

	sq_poptop(vm);

	return array;
}

Ref<SquirrelThread> SquirrelVMBase::create_thread() {
	GET_VM(Ref<SquirrelThread>());

	HSQUIRRELVM thread_vm = sq_newthread(vm, SQUIRREL_INITIAL_STACK_SIZE);
	ERR_FAIL_NULL_V(thread_vm, Ref<SquirrelThread>());
	const Ref<SquirrelThread> thread = get_stack(-1);
	DEV_ASSERT(thread.is_valid());

	sq_poptop(vm);

	return thread;
}

Ref<SquirrelUserData> SquirrelVMBase::wrap_variant(const Variant &p_value) {
	GET_VM(Ref<SquirrelUserData>());

	SquirrelVariantUserData::create(vm, p_value);

	const Ref<SquirrelUserData> ud = get_stack(-1);
	DEV_ASSERT(ud.is_valid());

	sq_poptop(vm);

	return ud;
}

Ref<SquirrelUserData> SquirrelVMBase::intern_variant(const Variant &p_value) {
	GET_VM(Ref<SquirrelUserData>());
	GET_OUTER_VM();

	auto it = outer_vm->_vm_internal->memoized_variants.find(p_value);
	if (it != outer_vm->_vm_internal->memoized_variants.end()) {
		const Ref<SquirrelUserData> ud = it->value->get_object();
		if (ud.is_valid()) {
			return ud;
		}
	}

	const Ref<SquirrelUserData> wrapped = wrap_variant(p_value);
	outer_vm->_vm_internal->memoized_variants[p_value] = wrapped->weak_ref();

	return wrapped;
}

SQInteger SquirrelVMBase::SquirrelVMInternal::squirrel_callable_wrapper(HSQUIRRELVM vm) {
	Ref<SquirrelVM> outer_vm = reinterpret_cast<SquirrelVM *>(sq_getsharedforeignptr(vm));
	Ref<SquirrelVMBase> vm_base;
	if (outer_vm->_vm_internal->vm == vm) {
		vm_base = outer_vm;
	} else {
		// do some juggling to get a SquirrelVMBase that points to the correct thread
		sq_pushthread(outer_vm->_vm_internal->vm, vm);
		vm_base = outer_vm->get_stack(-1);
		DEV_ASSERT(Ref<SquirrelThread>(vm_base).is_valid());
		sq_poptop(outer_vm->_vm_internal->vm);
	}

	SQBool varargs = SQFalse;
	ERR_FAIL_COND_V(SQ_FAILED(sq_getbool(vm, -2, &varargs)), sq_throwerror(vm, "wrapped callable free variables invalid"));
	Callable func = SquirrelVariantUserData::get(vm, -1);
	ERR_FAIL_COND_V(!func.is_valid(), sq_throwerror(vm, "wrapped callable is invalid"));

	const int64_t nargs = sq_gettop(vm) - 2;
	Array args;
	args.resize(nargs);
	for (int64_t i = 0; i < nargs; i++) {
		args[i] = vm_base->get_stack(i + 1);
	}

	const Variant this_obj = args.pop_front();
	if (varargs) {
		args = Array::make(vm_base, this_obj, args);
	}

	const Variant result = func.callv(args);

	const Ref<SquirrelThrow> ex(result);
	if (ex.is_valid()) {
		if (likely(vm_base->push_stack(ex->get_exception()))) {
			return sq_throwobject(vm);
		}

		ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned SquirrelThrow with non-Squirrel exception value"));
	}

	const Ref<SquirrelTailCall> tc(result);
	if (tc.is_valid()) {
		const Ref<SquirrelFunction> tcfunc = tc->get_func();
		if (likely(tcfunc.is_valid() && tcfunc->is_owned_by(outer_vm))) {
			sq_pushobject(vm, tcfunc->_internal->obj);
		} else if (tcfunc.is_null()) {
			ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned SquirrelTailCall with null function"));
		} else {
			ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned SquirrelTailCall with invalid function for this VM"));
		}

		const Array tcargs = tc->get_args();
		for (int64_t i = 0; i < tcargs.size(); i++) {
			if (unlikely(!vm_base->push_stack(tcargs[i]))) {
				sq_pop(vm, i + 1);
				ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned SquirrelTailCall with non-Squirrel arguments"));
			}
		}

		return sq_tailcall(vm, tcargs.size());
	}

	const Ref<SquirrelSuspend> sus(result);
	if (sus.is_valid()) {
		if (likely(vm_base->push_stack(sus->get_result()))) {
			return sq_suspendvm(vm);
		}

		ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned SquirrelSuspend with non-Squirrel result value"));
	}

	if (likely(vm_base->push_stack(result))) {
		return 1;
	}

	DEV_ASSERT(Ref<SquirrelSpecialReturn>(result).is_null());

	ERR_FAIL_V(sq_throwerror(vm, "wrapped callable returned non-Squirrel value"));
}

Ref<SquirrelNativeFunction> SquirrelVMBase::wrap_callable(const Callable &p_callable, bool p_varargs) {
	ERR_FAIL_COND_V(p_callable.is_null(), Ref<SquirrelNativeFunction>());
	GET_VM(Ref<SquirrelNativeFunction>());

	SquirrelVariantUserData::create(vm, p_callable);
	sq_pushbool(vm, p_varargs ? SQTrue : SQFalse);
	sq_newclosure(vm, &SquirrelVMInternal::squirrel_callable_wrapper, 2);
	sq_setnativeclosurename(vm, -1, String(p_callable.get_method()).utf8());

	const Ref<SquirrelNativeFunction> nf = get_stack(-1);
	DEV_ASSERT(nf.is_valid());

	sq_poptop(vm);

	return nf;
}

Variant SquirrelVMBase::_convert_variant_helper(const Variant &p_value, bool p_wrap_unhandled_values, bool &r_failed) {
	if (r_failed) {
		return Variant();
	}

	switch (p_value.get_type()) {
	case Variant::NIL:
	case Variant::BOOL:
	case Variant::INT:
	case Variant::FLOAT:
	case Variant::STRING:
	{
		return p_value;
	}
	case Variant::STRING_NAME:
	{
		return p_value.operator String();
	}
	case Variant::VECTOR2:
	case Variant::VECTOR2I:
	case Variant::RECT2:
	case Variant::RECT2I:
	case Variant::VECTOR3:
	case Variant::VECTOR3I:
	case Variant::TRANSFORM2D:
	case Variant::VECTOR4:
	case Variant::VECTOR4I:
	case Variant::PLANE:
	case Variant::QUATERNION:
	case Variant::AABB:
	case Variant::BASIS:
	case Variant::TRANSFORM3D:
	case Variant::PROJECTION:
	case Variant::COLOR:
	case Variant::NODE_PATH:
	case Variant::RID:
	case Variant::SIGNAL:
	case Variant::CALLABLE: // treat Callable as an opaque type to avoid giving Squirrel access to functions that might not understand Squirrel calls (and because we don't know which form of the calling convention it wants)
	case Variant::PACKED_VECTOR2_ARRAY:
	case Variant::PACKED_VECTOR3_ARRAY:
	case Variant::PACKED_COLOR_ARRAY:
	case Variant::PACKED_VECTOR4_ARRAY:
	{
		ERR_FAIL_COND_V_MSG(!p_wrap_unhandled_values, (r_failed = true, Variant()), vformat("Cannot convert %s %s to Squirrel value", Variant::get_type_name(p_value.get_type()), p_value));
		return wrap_variant(p_value);
	}
	case Variant::OBJECT:
	{
		if (Ref<SquirrelVariant>(p_value).is_valid() && Ref<SquirrelVariant>(p_value)->is_owned_by(this)) {
			return p_value;
		}
		Object *obj = p_value;
		ERR_FAIL_COND_V_MSG(!p_wrap_unhandled_values, (r_failed = true, Variant()), vformat("Cannot convert %s object %s to Squirrel value", obj ? obj->get_class() : "<null>", p_value));
		return wrap_variant(p_value);
	}
	case Variant::DICTIONARY:
	{
		const Dictionary dict = p_value;
		const Array keys = dict.keys();
		const Ref<SquirrelTable> table = create_table_with_initial_capacity(keys.size());
		for (int64_t i = 0; !r_failed && i < keys.size(); i++) {
			table->new_slot(_convert_variant_helper(keys[i], p_wrap_unhandled_values, r_failed), _convert_variant_helper(dict[keys[i]], p_wrap_unhandled_values, r_failed));
		}
		return r_failed ? Variant() : Variant(table);
	}
	case Variant::ARRAY:
	{
		const Array array = p_value;
		const Ref<SquirrelArray> squirrel_array = create_array(array.size());
		for (int64_t i = 0; !r_failed && i < array.size(); i++) {
			squirrel_array->set_item(i, _convert_variant_helper(array[i], p_wrap_unhandled_values, r_failed));
		}
		return r_failed ? Variant() : Variant(squirrel_array);
	}
	case Variant::PACKED_BYTE_ARRAY:
	case Variant::PACKED_INT32_ARRAY:
	case Variant::PACKED_INT64_ARRAY:
	case Variant::PACKED_FLOAT32_ARRAY:
	case Variant::PACKED_FLOAT64_ARRAY:
	case Variant::PACKED_STRING_ARRAY:
	{
		const Array wrapped_array = p_value;
		const Ref<SquirrelArray> packed_array = create_array(wrapped_array.size());
		for (int64_t i = 0; i < wrapped_array.size(); i++) {
			packed_array->set_item(i, wrapped_array[i]);
		}
		return packed_array;
	}
	case Variant::VARIANT_MAX:
		break;
	}

	ERR_FAIL_V_MSG((r_failed = true, Variant()), vformat("Unexpected Variant type %s", Variant::get_type_name(p_value.get_type())));
}

Variant SquirrelVMBase::convert_variant(const Variant &p_value, bool p_wrap_unhandled_values) {
	bool failed = false;
	const Variant result = _convert_variant_helper(p_value, p_wrap_unhandled_values, failed);
	return unlikely(failed) ? Variant() : result;
}

int64_t SquirrelVMBase::collect_garbage() {
	GET_VM(-1);

	return sq_collectgarbage(vm);
}

TypedArray<SquirrelVariant> SquirrelVMBase::resurrect_unreachable() {
	GET_VM(TypedArray<SquirrelVariant>());

	if (unlikely(SQ_FAILED(sq_resurrectunreachable(vm)))) {
		return TypedArray<SquirrelVariant>();
	}

	if (sq_gettype(vm, -1) == OT_NULL) {
		sq_poptop(vm);
		return TypedArray<SquirrelVariant>();
	}

	DEV_ASSERT(sq_gettype(vm, -1) == OT_ARRAY);

	TypedArray<SquirrelVariant> values;

	sq_pushnull(vm);
	while (SQ_SUCCEEDED(sq_next(vm, -2))) {
		values.append(get_stack(-1));
		sq_pop(vm, 2);
	}

	sq_pop(vm, 2);

	return values;
}

void SquirrelVMBase::set_error_handler(const Ref<SquirrelAnyFunction> &p_func) {
	GET_VM();

	ERR_FAIL_COND(!push_stack(p_func));
	sq_seterrorhandler(vm);
}

void SquirrelVMBase::set_error_handler_default() {
	GET_VM();

	sqstd_seterrorhandlers(vm);
}

void SquirrelVMBase::set_handle_caught_errors(bool p_enable) {
	GET_VM();

	sq_notifyallexceptions(vm, p_enable ? SQTrue : SQFalse);
}

Variant SquirrelVMBase::get_last_error() const {
	GET_VM(Variant());

	sq_getlasterror(vm);
	const Variant error = get_stack(-1);
	sq_poptop(vm);

	return error;
}

void SquirrelVMBase::reset_last_error() {
	GET_VM();

	sq_reseterror(vm);
}

#define SET_TABLE(m_name) \
	void SquirrelVMBase::set_##m_name##_table(const godot::Ref<SquirrelTable> &p_table) { \
		GET_VM(); \
		if (likely(p_table.is_valid())) { \
			ERR_FAIL_COND(!p_table->is_owned_by(this)); \
			sq_pushobject(vm, p_table->_internal->obj); \
		} else { \
			sq_pushnull(vm); \
		} \
		ERR_FAIL_COND(SQ_FAILED(sq_set##m_name##table(vm))); \
	}
#define GET_TABLE(m_name) \
	Ref<SquirrelTable> SquirrelVMBase::get_##m_name##_table() const { \
		GET_VM(Ref<SquirrelTable>()); \
		sq_push##m_name##table(vm); \
		const Variant m_name##_table = get_stack(-1); \
		DEV_ASSERT(m_name##_table == Variant() || Ref<SquirrelTable>(m_name##_table).is_valid()); \
		sq_poptop(vm); \
		return m_name##_table; \
	}

SET_TABLE(root);
GET_TABLE(root);
SET_TABLE(const);
GET_TABLE(const);
GET_TABLE(registry);

#undef SET_TABLE
#undef GET_TABLE

Ref<SquirrelStackInfo> SquirrelVMBase::get_stack_info(int64_t p_level) const {
	GET_VM(Ref<SquirrelStackInfo>());

	SQStackInfos si;
	if (unlikely(SQ_FAILED(sq_stackinfos(vm, p_level, &si)))) {
		return Ref<SquirrelStackInfo>();
	}

	Ref<SquirrelStackInfo> stack_info;
	stack_info.instantiate();

	if (si.source) {
		stack_info->_source = si.source;
	}
	if (si.funcname) {
		stack_info->_function_name = si.funcname;
	}
	stack_info->_line_number = si.line;

	godot_squirrel_push_call_closure(vm, p_level);
	stack_info->_function = get_stack(-1);
	sq_poptop(vm);

	int64_t i = 0;
	while (const SQChar *name = sq_getlocal(vm, p_level, i)) {
		stack_info->_local_variable_names.append(name);
		stack_info->_local_variable_values.append(get_stack(-1));
		sq_poptop(vm);
		i++;
	}

	return stack_info;
}

void SquirrelVMBase::print_call_stack() {
	GET_VM();

	sqstd_printcallstack(vm);
}

void SquirrelVM::_bind_methods() {
#ifndef SQUIRREL_NO_PRINT
	ClassDB::bind_method(D_METHOD("set_print_func", "print_func"), &SquirrelVM::set_print_func);
	ClassDB::bind_method(D_METHOD("get_print_func"), &SquirrelVM::get_print_func);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "print_func"), "set_print_func", "get_print_func");
	ClassDB::bind_method(D_METHOD("set_error_func", "error_func"), &SquirrelVM::set_error_func);
	ClassDB::bind_method(D_METHOD("get_error_func"), &SquirrelVM::get_error_func);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "error_func"), "set_error_func", "get_error_func");
#endif

#ifndef SQUIRREL_NO_DEBUG
	ADD_SIGNAL(MethodInfo("debug_call", PropertyInfo(Variant::OBJECT, "vm_or_thread", PROPERTY_HINT_RESOURCE_TYPE, SquirrelVMBase::get_class_static()), PropertyInfo(Variant::STRING, "source_name"), PropertyInfo(Variant::INT, "line"), PropertyInfo(Variant::STRING, "func_name")));
	ADD_SIGNAL(MethodInfo("debug_return", PropertyInfo(Variant::OBJECT, "vm_or_thread", PROPERTY_HINT_RESOURCE_TYPE, SquirrelVMBase::get_class_static()), PropertyInfo(Variant::STRING, "source_name"), PropertyInfo(Variant::INT, "line"), PropertyInfo(Variant::STRING, "func_name")));
	ADD_SIGNAL(MethodInfo("debug_line", PropertyInfo(Variant::OBJECT, "vm_or_thread", PROPERTY_HINT_RESOURCE_TYPE, SquirrelVMBase::get_class_static()), PropertyInfo(Variant::STRING, "source_name"), PropertyInfo(Variant::INT, "line"), PropertyInfo(Variant::STRING, "func_name")));

	ClassDB::bind_method(D_METHOD("set_debug_enabled", "debug_enabled"), &SquirrelVM::set_debug_enabled);
	ClassDB::bind_method(D_METHOD("is_debug_enabled"), &SquirrelVM::is_debug_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_enabled"), "set_debug_enabled", "is_debug_enabled");
#endif

	ClassDB::bind_method(D_METHOD("get_table_default_delegate"), &SquirrelVM::get_table_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "table_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_table_default_delegate");
	ClassDB::bind_method(D_METHOD("get_array_default_delegate"), &SquirrelVM::get_array_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "array_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_array_default_delegate");
	ClassDB::bind_method(D_METHOD("get_string_default_delegate"), &SquirrelVM::get_string_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "string_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_string_default_delegate");
	ClassDB::bind_method(D_METHOD("get_number_default_delegate"), &SquirrelVM::get_number_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "number_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_number_default_delegate");
	ClassDB::bind_method(D_METHOD("get_generator_default_delegate"), &SquirrelVM::get_generator_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "generator_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_generator_default_delegate");
	ClassDB::bind_method(D_METHOD("get_closure_default_delegate"), &SquirrelVM::get_closure_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "closure_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_closure_default_delegate");
	ClassDB::bind_method(D_METHOD("get_thread_default_delegate"), &SquirrelVM::get_thread_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "thread_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_thread_default_delegate");
	ClassDB::bind_method(D_METHOD("get_class_default_delegate"), &SquirrelVM::get_class_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "class_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_class_default_delegate");
	ClassDB::bind_method(D_METHOD("get_instance_default_delegate"), &SquirrelVM::get_instance_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "instance_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_instance_default_delegate");
	ClassDB::bind_method(D_METHOD("get_weak_ref_default_delegate"), &SquirrelVM::get_weak_ref_default_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "weak_ref_default_delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_READ_ONLY), "", "get_weak_ref_default_delegate");
}

SquirrelVM::SquirrelVM() : SquirrelVMBase(true) {
}

void SquirrelVM::set_print_func(const Callable &p_print_func) {
	_print_func = p_print_func;
}
Callable SquirrelVM::get_print_func() const {
	return _print_func;
}
void SquirrelVM::set_error_func(const Callable &p_error_func) {
	_error_func = p_error_func;
}
Callable SquirrelVM::get_error_func() const {
	return _error_func;
}

#ifndef SQUIRREL_NO_DEBUG
void SquirrelVM::set_debug_enabled(bool p_debug_enabled) {
	if (p_debug_enabled && !_debug_enabled) {
		sq_setnativedebughook(_vm_internal->vm, &SquirrelVMInternal::debug_hook);
	} else if (!p_debug_enabled && _debug_enabled) {
		sq_setnativedebughook(_vm_internal->vm, nullptr);
	}

	_debug_enabled = p_debug_enabled;
}

bool SquirrelVM::is_debug_enabled() const {
	return _debug_enabled;
}
#endif

Ref<SquirrelTable> SquirrelVM::get_table_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_TABLE)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_array_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_ARRAY)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_string_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_STRING)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_number_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_INTEGER)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_generator_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_GENERATOR)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_closure_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_CLOSURE)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_thread_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_THREAD)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_class_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_CLASS)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_instance_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_INSTANCE)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}
Ref<SquirrelTable> SquirrelVM::get_weak_ref_default_delegate() const {
	ERR_FAIL_COND_V(SQ_FAILED(sq_getdefaultdelegate(_vm_internal->vm, OT_WEAKREF)), Ref<SquirrelTable>());
	const Ref<SquirrelTable> delegate = get_stack(-1);
	sq_poptop(_vm_internal->vm);
	return delegate;
}

String SquirrelVM::_to_string() const {
	return vformat("<%s:%d>", get_class(), get_instance_id());
}

void SquirrelVariant::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_owned_by", "vm_or_thread"), &SquirrelVariant::is_owned_by);
	ClassDB::bind_method(D_METHOD("get_squirrel_reference_count"), &SquirrelVariant::get_squirrel_reference_count);
	ClassDB::bind_method(D_METHOD("iterate"), &SquirrelVariant::iterate);
	ClassDB::bind_method(D_METHOD("weak_ref"), &SquirrelVariant::weak_ref);
}

SquirrelVariant::SquirrelVariant() {
	_internal = memnew(SquirrelVariantInternal);
}

SquirrelVariant::~SquirrelVariant() {
	if (_vm.is_valid()) {
		DEV_ASSERT(_vm->_vm_internal);
		_vm->_vm_internal->ref_objects.erase(_internal->obj);
		sq_release(_vm->_vm_internal->vm, &_internal->obj);
	}
	memdelete(_internal);
}

bool SquirrelVariant::is_owned_by(const Ref<SquirrelVMBase> &p_vm_or_thread) const {
	ERR_FAIL_COND_V(p_vm_or_thread.is_null(), false);

	if (unlikely(_vm.is_null())) {
		const Ref<SquirrelVM> &this_vm = Ref<SquirrelVariant>(this);
		ERR_FAIL_COND_V_MSG(this_vm.is_valid(), false, "\"im not owned! im not owned!!\", i continue to insist as i slowly shrink and transform into a SquirrelVM");
		ERR_FAIL_V_MSG(false, "SquirrelVariant objects should not be created using .new()");
	}

	if (likely(p_vm_or_thread->_vm_internal)) {
		return p_vm_or_thread == _vm;
	}

	DEV_ASSERT(sq_isthread(p_vm_or_thread->_internal->obj));
	return p_vm_or_thread->_vm == _vm;
}

uint64_t SquirrelVariant::get_squirrel_reference_count() const {
	ERR_FAIL_COND_V(sq_isnull(_internal->obj), 0);

	return sq_getrefcount(_vm->_vm_internal->vm, &_internal->obj);
}

Ref<SquirrelIterator> SquirrelVariant::iterate() const {
	ERR_FAIL_COND_V(sq_isnull(_internal->obj), Ref<SquirrelIterator>());

	Ref<SquirrelIterator> iter;
	iter.instantiate();
	iter->_container = this;

	return iter;
}

Ref<SquirrelWeakRef> SquirrelVariant::weak_ref() const {
	ERR_FAIL_COND_V(sq_isnull(_internal->obj), Ref<SquirrelWeakRef>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_weakref(_vm->_vm_internal->vm, -1);
	const Ref<SquirrelWeakRef> ref = _vm->get_stack(-1);
	DEV_ASSERT(ref.is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return ref;
}

String SquirrelVariant::_to_string() const {
	if (likely(!sq_isnull(_internal->obj))) {
		DEV_ASSERT(_vm.is_valid());

		sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
		if (unlikely(SQ_FAILED(sq_tostring(_vm->_vm_internal->vm, -1)))) {
			sq_poptop(_vm->_vm_internal->vm);
		} else {
			const SQChar *string_data = nullptr;
			SQInteger string_size = 0;
			if (likely(SQ_SUCCEEDED(sq_getstringandsize(_vm->_vm_internal->vm, -1, &string_data, &string_size)))) {
				String string = String::utf8(string_data, string_size);
				sq_pop(_vm->_vm_internal->vm, 2);
				return string;
			}

			sq_pop(_vm->_vm_internal->vm, 2);
		}

		return vformat("<error %s:%d>", get_class(), get_instance_id());
	}

	return vformat("<uninitialized %s:%d>", get_class(), get_instance_id());
}

void SquirrelTable::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_delegate", "delegate"), &SquirrelTable::set_delegate);
	ClassDB::bind_method(D_METHOD("get_delegate"), &SquirrelTable::get_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_NONE), "set_delegate", "get_delegate");

	ClassDB::bind_method(D_METHOD("new_slot", "key", "value"), &SquirrelTable::new_slot);
	ClassDB::bind_method(D_METHOD("set_slot", "key", "value", "raw"), &SquirrelTable::set_slot, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("has_slot", "key", "raw"), &SquirrelTable::has_slot, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_slot", "key", "raw"), &SquirrelTable::get_slot, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("delete_slot", "key", "raw"), &SquirrelTable::delete_slot, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("size"), &SquirrelTable::size);
	ClassDB::bind_method(D_METHOD("clear"), &SquirrelTable::clear);
	ClassDB::bind_method(D_METHOD("duplicate"), &SquirrelTable::duplicate);
	ClassDB::bind_method(D_METHOD("wrap_callables", "callables", "varargs"), &SquirrelTable::wrap_callables);
}

bool SquirrelTable::set_delegate(const Ref<SquirrelTable> &p_delegate) {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);
	ERR_FAIL_COND_V(p_delegate.is_valid() && !p_delegate->is_owned_by(_vm), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (p_delegate.is_valid()) {
		sq_pushobject(_vm->_vm_internal->vm, p_delegate->_internal->obj);
	} else {
		sq_pushnull(_vm->_vm_internal->vm);
	}
	bool ok = SQ_SUCCEEDED(sq_setdelegate(_vm->_vm_internal->vm, -2));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

Ref<SquirrelTable> SquirrelTable::get_delegate() const {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), Ref<SquirrelTable>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_getdelegate(_vm->_vm_internal->vm, -1);
	const Variant delegate = _vm->get_stack(-1);
	DEV_ASSERT(delegate == Variant() || Ref<SquirrelTable>(delegate).is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return delegate;
}

bool SquirrelTable::new_slot(const Variant &p_key, const Variant &p_value) {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_key))) {
		sq_poptop(_vm->_vm_internal->vm);

		return false;
	}

	if (unlikely(!_vm->push_stack(p_value))) {
		sq_pop(_vm->_vm_internal->vm, 2);

		return false;
	}

	const bool ok = SQ_SUCCEEDED(sq_newslot(_vm->_vm_internal->vm, -3, SQFalse));
	sq_pop(_vm->_vm_internal->vm, ok ? 1 : 3);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

bool SquirrelTable::set_slot(const Variant &p_key, const Variant &p_value, bool p_raw) {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_key))) {
		sq_poptop(_vm->_vm_internal->vm);

		return false;
	}

	if (unlikely(!_vm->push_stack(p_value))) {
		sq_pop(_vm->_vm_internal->vm, 2);

		return false;
	}

	const bool ok = p_raw ?
		SQ_SUCCEEDED(sq_rawset(_vm->_vm_internal->vm, -3)) :
		SQ_SUCCEEDED(sq_set(_vm->_vm_internal->vm, -3));
	sq_pop(_vm->_vm_internal->vm, ok || p_raw ? 1 : 3);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

bool SquirrelTable::has_slot(const Variant &p_key, bool p_raw) const {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_key))) {
		sq_poptop(_vm->_vm_internal->vm);

		return false;
	}

	const bool ok = p_raw ?
		SQ_SUCCEEDED(sq_rawget(_vm->_vm_internal->vm, -2)) :
		SQ_SUCCEEDED(sq_get(_vm->_vm_internal->vm, -2));

	if (likely(ok)) {
		sq_pop(_vm->_vm_internal->vm, 2);

		return true;
	}

	sq_poptop(_vm->_vm_internal->vm);

	return false;
}

Variant SquirrelTable::get_slot(const Variant &p_key, bool p_raw) const {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), Variant());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_key))) {
		sq_poptop(_vm->_vm_internal->vm);

		return Variant();
	}

	const bool ok = p_raw ?
		SQ_SUCCEEDED(sq_rawget(_vm->_vm_internal->vm, -2)) :
		SQ_SUCCEEDED(sq_get(_vm->_vm_internal->vm, -2));

	if (likely(ok)) {
		const Variant value = _vm->get_stack(-1);

		sq_pop(_vm->_vm_internal->vm, 2);

		return value;
	}

	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_V(Variant());
}

void SquirrelTable::delete_slot(const Variant &p_key, bool p_raw) {
	ERR_FAIL_COND(!sq_istable(_internal->obj));

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_key))) {
		sq_poptop(_vm->_vm_internal->vm);

		return;
	}

	if (p_raw) {
		sq_rawdeleteslot(_vm->_vm_internal->vm, -2, SQFalse);
	} else {
		sq_deleteslot(_vm->_vm_internal->vm, -2, SQFalse);
	}

	sq_poptop(_vm->_vm_internal->vm);
}

int64_t SquirrelTable::size() const {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	const SQInteger size = sq_getsize(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);

	return size;
}

void SquirrelTable::clear() {
	ERR_FAIL_COND(!sq_istable(_internal->obj));

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_clear(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);
}

Ref<SquirrelTable> SquirrelTable::duplicate() const {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), Ref<SquirrelTable>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	ERR_FAIL_COND_V(SQ_FAILED(sq_clone(_vm->_vm_internal->vm, -1)), (sq_poptop(_vm->_vm_internal->vm), Ref<SquirrelTable>()));
	const Ref<SquirrelTable> table = _vm->get_stack(-1);
	DEV_ASSERT(table.is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return table;
}

bool SquirrelTable::wrap_callables(const TypedDictionary<String, Callable> &p_callables, bool p_varargs) {
	ERR_FAIL_COND_V(!sq_istable(_internal->obj), false);

	const PackedStringArray keys(p_callables.keys());

	// create functions before assigning any so we can be atomic
	HashMap<String, Ref<SquirrelNativeFunction>> functions;
	for (int64_t i = 0; i < keys.size(); i++) {
		const Ref<SquirrelNativeFunction> func = _vm->wrap_callable(p_callables[keys[i]], p_varargs);
		ERR_FAIL_COND_V(func.is_null(), false);
		functions.insert(keys[i], func);
	}

	bool ok = true;
	for (int64_t i = 0; i < keys.size(); i++) {
		// keep going even if it fails at this point
		ok = new_slot(keys[i], functions.get(keys[i])) && ok;
	}

	return ok;
}

void SquirrelArray::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_item", "index", "value"), &SquirrelArray::set_item);
	ClassDB::bind_method(D_METHOD("get_item", "index"), &SquirrelArray::get_item);
	ClassDB::bind_method(D_METHOD("append", "value"), &SquirrelArray::append);
	ClassDB::bind_method(D_METHOD("insert", "index", "value"), &SquirrelArray::insert);
	ClassDB::bind_method(D_METHOD("remove", "index"), &SquirrelArray::remove);
	ClassDB::bind_method(D_METHOD("pop_back"), &SquirrelArray::pop_back);
	ClassDB::bind_method(D_METHOD("resize", "size"), &SquirrelArray::resize);
	ClassDB::bind_method(D_METHOD("size"), &SquirrelArray::size);
	ClassDB::bind_method(D_METHOD("reverse"), &SquirrelArray::reverse);
	ClassDB::bind_method(D_METHOD("clear"), &SquirrelArray::clear);
	ClassDB::bind_method(D_METHOD("duplicate"), &SquirrelArray::duplicate);
}

bool SquirrelArray::set_item(int64_t p_index, const Variant &p_value) {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_pushinteger(_vm->_vm_internal->vm, p_index);

	if (unlikely(!_vm->push_stack(p_value))) {
		sq_pop(_vm->_vm_internal->vm, 2);
		return false;
	}

	if (unlikely(SQ_FAILED(sq_rawset(_vm->_vm_internal->vm, -3)))) {
		sq_pop(_vm->_vm_internal->vm, 3);
		return false;
	}

	sq_poptop(_vm->_vm_internal->vm);
	return true;
}

Variant SquirrelArray::get_item(int64_t p_index) const {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), Variant());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_pushinteger(_vm->_vm_internal->vm, p_index);
	if (unlikely(SQ_FAILED(sq_rawget(_vm->_vm_internal->vm, -2)))) {
		sq_poptop(_vm->_vm_internal->vm);
		ERR_FAIL_V(Variant());
	}

	const Variant item = _vm->get_stack(-1);
	sq_pop(_vm->_vm_internal->vm, 2);

	return item;
}

bool SquirrelArray::append(const Variant &p_value) {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_value))) {
		sq_poptop(_vm->_vm_internal->vm);
		return false;
	}

	bool ok = SQ_SUCCEEDED(sq_arrayappend(_vm->_vm_internal->vm, -1));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

bool SquirrelArray::insert(int64_t p_index, const Variant &p_value) {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_value))) {
		sq_poptop(_vm->_vm_internal->vm);
		return false;
	}

	bool ok = SQ_SUCCEEDED(sq_arrayinsert(_vm->_vm_internal->vm, -1, p_index));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

bool SquirrelArray::remove(int64_t p_index) {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	bool ok = SQ_SUCCEEDED(sq_arrayremove(_vm->_vm_internal->vm, -1, p_index));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

Variant SquirrelArray::pop_back() {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), Variant());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (likely(SQ_SUCCEEDED(sq_arraypop(_vm->_vm_internal->vm, -1, SQTrue)))) {
		const Variant item = _vm->get_stack(-1);
		sq_pop(_vm->_vm_internal->vm, 2);

		return item;
	}

	sq_poptop(_vm->_vm_internal->vm);
	ERR_FAIL_V(Variant());
}

bool SquirrelArray::resize(int64_t p_size) {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	bool ok = SQ_SUCCEEDED(sq_arrayresize(_vm->_vm_internal->vm, -1, p_size));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

int64_t SquirrelArray::size() const {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), 0);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	const SQInteger size = sq_getsize(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);

	return size;
}

void SquirrelArray::reverse() {
	ERR_FAIL_COND(!sq_isarray(_internal->obj));

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_arrayreverse(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);
}

void SquirrelArray::clear() {
	ERR_FAIL_COND(!sq_isarray(_internal->obj));

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_clear(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);
}

Ref<SquirrelArray> SquirrelArray::duplicate() const {
	ERR_FAIL_COND_V(!sq_isarray(_internal->obj), Ref<SquirrelArray>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	ERR_FAIL_COND_V(SQ_FAILED(sq_clone(_vm->_vm_internal->vm, -1)), (sq_poptop(_vm->_vm_internal->vm), Ref<SquirrelArray>()));
	const Ref<SquirrelArray> arr = _vm->get_stack(-1);
	DEV_ASSERT(arr.is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return arr;
}

void SquirrelUserData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_delegate", "delegate"), &SquirrelUserData::set_delegate);
	ClassDB::bind_method(D_METHOD("get_delegate"), &SquirrelUserData::get_delegate);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "delegate", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_NONE), "set_delegate", "get_delegate");

	ClassDB::bind_method(D_METHOD("is_variant"), &SquirrelUserData::is_variant);
	ClassDB::bind_method(D_METHOD("get_variant"), &SquirrelUserData::get_variant);
}

bool SquirrelUserData::set_delegate(const Ref<SquirrelTable> &p_delegate) {
	ERR_FAIL_COND_V(!sq_isuserdata(_internal->obj), false);
	ERR_FAIL_COND_V(p_delegate.is_valid() && !p_delegate->is_owned_by(_vm), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (p_delegate.is_valid()) {
		sq_pushobject(_vm->_vm_internal->vm, p_delegate->_internal->obj);
	} else {
		sq_pushnull(_vm->_vm_internal->vm);
	}
	bool ok = SQ_SUCCEEDED(sq_setdelegate(_vm->_vm_internal->vm, -2));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

Ref<SquirrelTable> SquirrelUserData::get_delegate() const {
	ERR_FAIL_COND_V(!sq_isuserdata(_internal->obj), Ref<SquirrelTable>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_getdelegate(_vm->_vm_internal->vm, -1);
	const Variant delegate = _vm->get_stack(-1);
	DEV_ASSERT(delegate == Variant() || Ref<SquirrelTable>(delegate).is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return delegate;
}

bool SquirrelUserData::is_variant() const {
	SQUserPointer type_tag = nullptr;
	ERR_FAIL_COND_V(SQ_FAILED(sq_getobjtypetag(&_internal->obj, &type_tag)), false);

	return type_tag == SquirrelVariantUserData::type_tag;
}

Variant SquirrelUserData::get_variant() const {
	ERR_FAIL_COND_V(_vm.is_null(), Variant());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	const Variant value = SquirrelVariantUserData::get(_vm->_vm_internal->vm, -1);
	sq_poptop(_vm->_vm_internal->vm);

	return value;
}

void SquirrelCallable::_bind_methods() {
}

void SquirrelAnyFunction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_name"), &SquirrelAnyFunction::get_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_READ_ONLY), StringName(), "get_name");

	ClassDB::bind_method(D_METHOD("bind_env", "env"), &SquirrelAnyFunction::bind_env);
}

String SquirrelAnyFunction::get_name() const {
	ERR_FAIL_COND_V(_vm.is_null(), String());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);

	if (unlikely(SQ_FAILED(sq_getclosurename(_vm->_vm_internal->vm, -1)))) {
		sq_poptop(_vm->_vm_internal->vm);
		ERR_FAIL_V_MSG(String(), "the target is not a closure");
	}

	const String name = _vm->get_stack(-1);
	sq_pop(_vm->_vm_internal->vm, 2);

	return name;
}

Ref<SquirrelAnyFunction> SquirrelAnyFunction::bind_env(const Ref<SquirrelVariant> &p_env) const {
	ERR_FAIL_COND_V(_vm.is_null(), Ref<SquirrelAnyFunction>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_env))) {
		sq_poptop(_vm->_vm_internal->vm);
		return Ref<SquirrelAnyFunction>(); // push_stack already wrote an error message
	}

	if (unlikely(SQ_FAILED(sq_bindenv(_vm->_vm_internal->vm, -2)))) {
		sq_pop(_vm->_vm_internal->vm, 2);
		ERR_FAIL_V_MSG(Ref<SquirrelAnyFunction>(), _vm->get_last_error().stringify());
	}

	const Ref<SquirrelAnyFunction> bound = _vm->get_stack(-1);
	sq_pop(_vm->_vm_internal->vm, 2);

	return bound;
}

void SquirrelFunction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_root_table", "root_table"), &SquirrelFunction::set_root_table);
	ClassDB::bind_method(D_METHOD("get_root_table"), &SquirrelFunction::get_root_table);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "root_table", PROPERTY_HINT_RESOURCE_TYPE, SquirrelTable::get_class_static(), PROPERTY_USAGE_NONE), "set_root_table", "get_root_table");

	ClassDB::bind_method(D_METHOD("get_outer_values"), &SquirrelFunction::get_outer_values);
}

void SquirrelFunction::set_root_table(const Ref<SquirrelTable> &p_root_table) {
	ERR_FAIL_COND(_vm.is_null());
	ERR_FAIL_COND(p_root_table.is_null());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(!_vm->push_stack(p_root_table))) {
		sq_poptop(_vm->_vm_internal->vm);
		return;
	}

	if (unlikely(SQ_FAILED(sq_setclosureroot(_vm->_vm_internal->vm, -2)))) {
		sq_pop(_vm->_vm_internal->vm, 2);
		ERR_FAIL_MSG(_vm->get_last_error().stringify());
	}

	sq_poptop(_vm->_vm_internal->vm);
}

Ref<SquirrelTable> SquirrelFunction::get_root_table() const {
	ERR_FAIL_COND_V(_vm.is_null(), Ref<SquirrelTable>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(SQ_FAILED(sq_getclosureroot(_vm->_vm_internal->vm, -1)))) {
		sq_poptop(_vm->_vm_internal->vm);
		ERR_FAIL_V_MSG(Ref<SquirrelTable>(), _vm->get_last_error().stringify());
	}

	const Ref<SquirrelTable> root = _vm->get_stack(-1);
	sq_pop(_vm->_vm_internal->vm, 2);

	return root;
}

Array SquirrelFunction::get_outer_values() const {
	ERR_FAIL_COND_V(_vm.is_null(), Array());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	SQInteger nparams = 0, nfreevars = 0;
	if (unlikely(SQ_FAILED(sq_getclosureinfo(_vm->_vm_internal->vm, -1, &nparams, &nfreevars)))) {
		sq_poptop(_vm->_vm_internal->vm);
		ERR_FAIL_V_MSG(Array(), _vm->get_last_error().stringify());
	}

	Array array;
	array.resize(nfreevars);

	for (SQInteger i = 0; i < nfreevars; i++) {
		if (likely(sq_getfreevariable(_vm->_vm_internal->vm, -1, i))) {
			array[i] = _vm->get_stack(-1);
			sq_poptop(_vm->_vm_internal->vm);
		}
	}

	sq_poptop(_vm->_vm_internal->vm);

	return array;
}

void SquirrelNativeFunction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_name", "name"), &SquirrelNativeFunction::set_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "set_name", "get_name");

	ClassDB::bind_method(D_METHOD("set_params_check", "num_args", "type_mask"), &SquirrelNativeFunction::set_params_check, DEFVAL(String()));
}

void SquirrelNativeFunction::set_name(const String &p_name) {
	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	sq_setnativeclosurename(_vm->_vm_internal->vm, -1, p_name.utf8());
	sq_poptop(_vm->_vm_internal->vm);
}

bool SquirrelNativeFunction::set_params_check(int64_t p_num_args, const String &p_type_mask) {
	ERR_FAIL_COND_V(!sq_isnativeclosure(_internal->obj), false);

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	bool ok = SQ_SUCCEEDED(sq_setparamscheck(_vm->_vm_internal->vm, p_num_args == 0 ? SQ_MATCHTYPEMASKSTRING : p_num_args, p_type_mask.utf8()));
	sq_poptop(_vm->_vm_internal->vm);

	ERR_FAIL_COND_V_MSG(!ok, false, _vm->get_last_error().stringify());

	return ok;
}

void SquirrelGenerator::_bind_methods() {
	BIND_ENUM_CONSTANT(DEAD);
	static_assert(DEAD == SQ_VMSTATE_IDLE);
	BIND_ENUM_CONSTANT(RUNNING);
	static_assert(RUNNING == SQ_VMSTATE_RUNNING);
	BIND_ENUM_CONSTANT(SUSPENDED);
	static_assert(SUSPENDED == SQ_VMSTATE_SUSPENDED);

	ClassDB::bind_method(D_METHOD("get_state"), &SquirrelGenerator::get_state);
}

SquirrelGenerator::GeneratorState SquirrelGenerator::get_state() const {
	ERR_FAIL_COND_V(!sq_isgenerator(_internal->obj), DEAD);

	return static_cast<SquirrelGenerator::GeneratorState>(godot_squirrel_get_generator_state(&_internal->obj));
}

void SquirrelThread::_bind_methods() {
}

void SquirrelClass::_bind_methods() {
}

void SquirrelInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("duplicate"), &SquirrelInstance::duplicate);
}

Ref<SquirrelInstance> SquirrelInstance::duplicate() const {
	ERR_FAIL_COND_V(!sq_isinstance(_internal->obj), Ref<SquirrelInstance>());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	ERR_FAIL_COND_V(SQ_FAILED(sq_clone(_vm->_vm_internal->vm, -1)), (sq_poptop(_vm->_vm_internal->vm), Ref<SquirrelInstance>()));
	const Ref<SquirrelInstance> inst = _vm->get_stack(-1);
	DEV_ASSERT(inst.is_valid());
	sq_pop(_vm->_vm_internal->vm, 2);

	return inst;
}

void SquirrelWeakRef::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_object"), &SquirrelWeakRef::get_object);
}

Variant SquirrelWeakRef::get_object() const {
	ERR_FAIL_COND_V(!sq_isweakref(_internal->obj), Variant());

	sq_pushobject(_vm->_vm_internal->vm, _internal->obj);
	if (unlikely(SQ_FAILED(sq_getweakrefval(_vm->_vm_internal->vm, -1)))) {
		sq_poptop(_vm->_vm_internal->vm);
		ERR_FAIL_V(Variant());
	}

	const Variant object = _vm->get_stack(-1);
	sq_pop(_vm->_vm_internal->vm, 2);

	return object;
}

void SquirrelIterator::_bind_methods() {
	ClassDB::bind_method(D_METHOD("next"), &SquirrelIterator::next);

	ClassDB::bind_method(D_METHOD("set_key", "key"), &SquirrelIterator::set_key);
	ClassDB::bind_method(D_METHOD("get_key"), &SquirrelIterator::get_key);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "key", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_key", "get_key");

	ClassDB::bind_method(D_METHOD("set_value", "value"), &SquirrelIterator::set_value);
	ClassDB::bind_method(D_METHOD("get_value"), &SquirrelIterator::get_value);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_value", "get_value");
}

bool SquirrelIterator::next() {
	ERR_FAIL_COND_V(_container.is_null(), false);

	_container->_vm->push_stack(_container);
	_container->_vm->push_stack(_iterator);

	if (likely(SQ_SUCCEEDED(sq_next(_container->_vm->_vm_internal->vm, -2)))) {
		_iterator = _container->_vm->get_stack(-3);
		_key = _container->_vm->get_stack(-2);
		_value = _container->_vm->get_stack(-1);

		sq_pop(_container->_vm->_vm_internal->vm, 4);

		return true;
	}

	_iterator = _container->_vm->get_stack(-1);
	_key = Variant();
	_value = Variant();

	sq_pop(_container->_vm->_vm_internal->vm, 2);

	return false;
}

void SquirrelIterator::set_key(const Variant &p_key) {
	_key = p_key;
}
Variant SquirrelIterator::get_key() const {
	return _key;
}

void SquirrelIterator::set_value(const Variant &p_value) {
	_value = p_value;
}
Variant SquirrelIterator::get_value() const {
	return _value;
}

void SquirrelSpecialReturn::_bind_methods() {
}

void SquirrelThrow::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_exception", "exception"), &SquirrelThrow::set_exception);
	ClassDB::bind_method(D_METHOD("get_exception"), &SquirrelThrow::get_exception);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "exception", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_exception", "get_exception");

	ClassDB::bind_static_method(get_class_static(), D_METHOD("make", "exception"), &SquirrelThrow::make);
}

void SquirrelThrow::set_exception(const Variant &p_exception) {
	_exception = p_exception;
}

Variant SquirrelThrow::get_exception() const {
	return _exception;
}

Ref<SquirrelThrow> SquirrelThrow::make(const Variant &p_exception) {
	Ref<SquirrelThrow> ex;
	ex.instantiate();
	ex->set_exception(p_exception);
	return ex;
}

void SquirrelTailCall::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_func", "func"), &SquirrelTailCall::set_func);
	ClassDB::bind_method(D_METHOD("get_func"), &SquirrelTailCall::get_func);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "func", PROPERTY_HINT_RESOURCE_TYPE, SquirrelFunction::get_class_static()), "set_func", "get_func");

	ClassDB::bind_method(D_METHOD("set_args", "args"), &SquirrelTailCall::set_args);
	ClassDB::bind_method(D_METHOD("get_args"), &SquirrelTailCall::get_args);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "args"), "set_args", "get_args");

	ClassDB::bind_static_method(get_class_static(), D_METHOD("make", "func", "args"), &SquirrelTailCall::make);
}

void SquirrelTailCall::set_func(const Ref<SquirrelFunction> &p_func) {
	_func = p_func;
}

Ref<SquirrelFunction> SquirrelTailCall::get_func() const {
	return _func;
}

void SquirrelTailCall::set_args(const Array &p_args) {
	_args = p_args;
}

Array SquirrelTailCall::get_args() const {
	return _args;
}

Ref<SquirrelTailCall> SquirrelTailCall::make(const Ref<SquirrelFunction> &p_func, const Array &p_args) {
	ERR_FAIL_COND_V(p_func.is_null(), Ref<SquirrelTailCall>());
	ERR_FAIL_COND_V(p_args.is_empty(), Ref<SquirrelTailCall>());

	Ref<SquirrelTailCall> tc;
	tc.instantiate();
	tc->set_func(p_func);
	tc->set_args(p_args);
	return tc;
}

void SquirrelSuspend::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_result", "result"), &SquirrelSuspend::set_result);
	ClassDB::bind_method(D_METHOD("get_result"), &SquirrelSuspend::get_result);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT), "set_result", "get_result");

	ClassDB::bind_static_method(get_class_static(), D_METHOD("make", "result"), &SquirrelSuspend::make);
}

void SquirrelSuspend::set_result(const Variant &p_result) {
	_result = p_result;
}

Variant SquirrelSuspend::get_result() const {
	return _result;
}

Ref<SquirrelSuspend> SquirrelSuspend::make(const Variant &p_result) {
	Ref<SquirrelSuspend> sus;
	sus.instantiate();
	sus->set_result(p_result);
	return sus;
}

// TODO:
//
// DEBUG
// SQUIRREL_API const SQChar *sq_getfreevariable(HSQUIRRELVM v,SQInteger idx,SQUnsignedInteger nval);
// SQUIRREL_API SQRESULT sq_setfreevariable(HSQUIRRELVM v,SQInteger idx,SQUnsignedInteger nval);
// SQUIRREL_API SQRESULT sq_getclosureinfo(HSQUIRRELVM v,SQInteger idx,SQInteger *nparams,SQInteger *nfreevars);
//
// CLASS/INSTANCE
// SQUIRREL_API SQRESULT sq_getbase(HSQUIRRELVM v,SQInteger idx);
// SQUIRREL_API SQBool sq_instanceof(HSQUIRRELVM v);
// SQUIRREL_API SQRESULT sq_newmember(HSQUIRRELVM v,SQInteger idx,SQBool bstatic);
// SQUIRREL_API SQRESULT sq_rawnewmember(HSQUIRRELVM v,SQInteger idx,SQBool bstatic);
// SQUIRREL_API SQRESULT sq_getmemberhandle(HSQUIRRELVM v,SQInteger idx,HSQMEMBERHANDLE *handle);
// SQUIRREL_API SQRESULT sq_getbyhandle(HSQUIRRELVM v,SQInteger idx,const HSQMEMBERHANDLE *handle);
// SQUIRREL_API SQRESULT sq_setbyhandle(HSQUIRRELVM v,SQInteger idx,const HSQMEMBERHANDLE *handle);
// SQUIRREL_API SQRESULT sq_setattributes(HSQUIRRELVM v,SQInteger idx);
// SQUIRREL_API SQRESULT sq_getattributes(HSQUIRRELVM v,SQInteger idx);
// SQUIRREL_API SQRESULT sq_getclass(HSQUIRRELVM v,SQInteger idx);
