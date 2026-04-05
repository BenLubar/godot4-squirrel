#pragma once

#include <gdextension_interface.h>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_dictionary.hpp>
#ifndef SQUIRREL_NO_IMPORTER
#include <godot_cpp/classes/editor_import_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#endif

#ifndef SQUIRREL_INITIAL_STACK_SIZE
#define SQUIRREL_INITIAL_STACK_SIZE 128
#endif

class SquirrelThrow;

class SquirrelScript : public godot::Resource {
	GDCLASS(SquirrelScript, godot::Resource);

protected:
	static void _bind_methods();

private:
	godot::String _source;
	godot::PackedByteArray _bytecode;

	godot::String _error_desc;
	godot::String _error_source;
	int64_t _error_line = -1;
	int64_t _error_column = -1;

public:
	[[nodiscard]] godot::String get_source() const;
	void set_source(const godot::String &p_source);

	[[nodiscard]] godot::PackedByteArray get_bytecode() const;
	void set_bytecode(const godot::PackedByteArray &p_bytecode);

	[[nodiscard]] godot::String get_error_desc() const;
	[[nodiscard]] godot::String get_error_source() const;
	[[nodiscard]] int64_t get_error_line() const;
	[[nodiscard]] int64_t get_error_column() const;

	void set_error_desc(const godot::String &p_error_desc);
	void set_error_source(const godot::String &p_error_source);
	void set_error_line(int64_t p_error_line);
	void set_error_column(int64_t p_error_column);

	godot::Error compile(const godot::String &p_debug_file_name = godot::String());
};

#ifndef SQUIRREL_NO_IMPORTER
class SquirrelEditorImportPlugin : public godot::EditorImportPlugin {
	GDCLASS(SquirrelEditorImportPlugin, godot::EditorImportPlugin);

protected:
	static void _bind_methods();

public:
	[[nodiscard]] godot::String _get_importer_name() const override;
	[[nodiscard]] godot::String _get_visible_name() const override;
	[[nodiscard]] int32_t _get_preset_count() const override;
	[[nodiscard]] godot::String _get_preset_name(int32_t p_preset_index) const override;
	[[nodiscard]] godot::PackedStringArray _get_recognized_extensions() const override;
	[[nodiscard]] godot::TypedArray<godot::Dictionary> _get_import_options(const godot::String &p_path, int32_t p_preset_index) const override;
	[[nodiscard]] godot::String _get_save_extension() const override;
	[[nodiscard]] godot::String _get_resource_type() const override;
	[[nodiscard]] float _get_priority() const override;
	[[nodiscard]] int32_t _get_import_order() const override;
	[[nodiscard]] int32_t _get_format_version() const override;
	[[nodiscard]] bool _get_option_visibility(const godot::String &p_path, const godot::StringName &p_option_name, const godot::Dictionary &p_options) const override;
	[[nodiscard]] godot::Error _import(const godot::String &p_source_file, const godot::String &p_save_path, const godot::Dictionary &p_options, const godot::TypedArray<godot::String> &p_platform_variants, const godot::TypedArray<godot::String> &p_gen_files) const override;
	[[nodiscard]] bool _can_import_threaded() const override;

	[[nodiscard]] godot::String _to_string() const;
};

class SquirrelEditorPlugin : public godot::EditorPlugin {
	GDCLASS(SquirrelEditorPlugin, godot::EditorPlugin);

protected:
	static void _bind_methods();

private:
	godot::Ref<SquirrelEditorImportPlugin> _importer;

public:
	SquirrelEditorPlugin();

	void _enter_tree() override;
	void _exit_tree() override;
};
#endif

class SquirrelVMBase;
class SquirrelVM;
class SquirrelTable;
class SquirrelArray;
class SquirrelUserData;
class SquirrelCallable;
class SquirrelAnyFunction;
class SquirrelFunction;
class SquirrelNativeFunction;
class SquirrelGenerator;
class SquirrelThread;
class SquirrelClass;
class SquirrelInstance;
class SquirrelWeakRef;
class SquirrelIterator;

#ifdef _MSC_VER
typedef __int64 SQInteger;
typedef unsigned __int64 SQUnsignedInteger;
#else
typedef long long SQInteger;
typedef unsigned long long SQUnsignedInteger;
#endif
typedef char SQChar;
typedef struct SQVM *HSQUIRRELVM;
typedef SQInteger (*SQFUNCTION)(HSQUIRRELVM);

class SquirrelVariant : public godot::RefCounted {
	GDCLASS(SquirrelVariant, godot::RefCounted);

protected:
	static void _bind_methods();

	struct VMHolder {
		godot::SafeRefCount ref_count;
		SquirrelVM *vm;
	};
	VMHolder *_vm = nullptr;
	[[nodiscard]] _FORCE_INLINE_ SquirrelVM *_get_vm() const { return likely(_vm) ? _vm->vm : nullptr; }

	struct SquirrelVariantInternal;
	friend struct SquirrelVariantInternal;
	friend class SquirrelVMBase;
	friend class SquirrelUserData;
	friend class SquirrelIterator;
	SquirrelVariantInternal *_internal = nullptr;

public:
	SquirrelVariant();
	~SquirrelVariant() override;

	[[nodiscard]] bool is_owned_by(const godot::Ref<SquirrelVMBase> &p_vm_or_thread) const;
	[[nodiscard]] uint64_t get_squirrel_reference_count() const;
	[[nodiscard]] godot::Ref<SquirrelIterator> iterate() const;
	[[nodiscard]] godot::Ref<SquirrelWeakRef> weak_ref() const;

	[[nodiscard]] godot::String _to_string() const;
};

class SquirrelStackInfo : public godot::RefCounted {
	GDCLASS(SquirrelStackInfo, godot::RefCounted);

protected:
	static void _bind_methods();

private:
	godot::String _source;
	godot::String _function_name;
	int64_t _line_number = -1;
	godot::Ref<SquirrelAnyFunction> _function;
	godot::PackedStringArray _local_variable_names;
	godot::Array _local_variable_values;

	friend class SquirrelVMBase;

public:
	[[nodiscard]] godot::String get_source() const;
	[[nodiscard]] godot::String get_function_name() const;
	[[nodiscard]] int64_t get_line_number() const;
	[[nodiscard]] godot::Ref<SquirrelAnyFunction> get_function() const;
	[[nodiscard]] godot::PackedStringArray get_local_variable_names() const;
	[[nodiscard]] godot::Array get_local_variable_values() const;
};

class SquirrelVMBase : public SquirrelVariant {
	GDCLASS(SquirrelVMBase, SquirrelVariant);

protected:
	static void _bind_methods();

	struct SquirrelVMInternal;
	friend struct SquirrelVMInternal;
	friend class SquirrelVariant;
	friend struct SquirrelVariant::SquirrelVariantInternal;
	friend class SquirrelTable;
	friend class SquirrelArray;
	friend class SquirrelUserData;
	friend class SquirrelAnyFunction;
	friend class SquirrelFunction;
	friend class SquirrelNativeFunction;
	friend class SquirrelClass;
	friend class SquirrelInstance;
	friend class SquirrelWeakRef;
	friend class SquirrelIterator;
	SquirrelVMInternal *_vm_internal = nullptr;

	SquirrelVMBase() : SquirrelVMBase(false) {}
	explicit SquirrelVMBase(bool create);
public:
	~SquirrelVMBase() override;

	enum VMState {
		IDLE = 0,
		RUNNING = 1,
		SUSPENDED = 2,
	};

	[[nodiscard]] godot::Ref<SquirrelFunction> import(const godot::Ref<SquirrelScript> &p_script, const godot::String &p_debug_file_name = godot::String());
	[[nodiscard]] godot::Ref<SquirrelFunction> import_script(const godot::String &p_script, const godot::String &p_debug_file_name = godot::String());
	void import_blob();
	void import_math();
	void import_string();
	godot::Variant call_function(const godot::Variant **p_args, GDExtensionInt p_arg_count, GDExtensionCallError &r_error);
	godot::Variant apply_function(const godot::Ref<SquirrelCallable> &p_func, const godot::Variant &p_this, const godot::Array &p_args);
	godot::Variant apply_function_catch(const godot::Ref<SquirrelCallable> &p_func, const godot::Variant &p_this, const godot::Array &p_args);
	godot::Variant resume_generator(const godot::Ref<SquirrelGenerator> &p_generator);

	[[nodiscard]] godot::Variant get_stack(int64_t p_index) const;
	[[nodiscard]] int64_t get_stack_top() const;
	bool push_stack(const godot::Variant &p_value);
	godot::Ref<SquirrelThrow> push_stack_or_error(const godot::Variant &p_value);
	static void push_stack_native(HSQUIRRELVM p_vm, const godot::Ref<SquirrelVariant> &p_value);
	void pop_stack(int64_t p_count = 1);
	void remove_stack(int64_t p_index);

	[[nodiscard]] VMState get_state() const;
	[[nodiscard]] bool is_suspended() const;
	godot::Variant wake_up(const godot::Variant &p_value = godot::Variant());
	godot::Variant wake_up_throw(const godot::Variant &p_exception);
	godot::Variant wake_up_catch(const godot::Variant &p_value = godot::Variant());
	godot::Variant wake_up_throw_catch(const godot::Variant &p_exception);

	[[nodiscard]] godot::Ref<SquirrelInstance> create_blob(const godot::PackedByteArray &p_data);
	[[nodiscard]] godot::Ref<SquirrelTable> create_table();
	[[nodiscard]] godot::Ref<SquirrelTable> create_table_with_initial_capacity(int64_t p_size);
	[[nodiscard]] godot::Ref<SquirrelArray> create_array(int64_t p_size);
	[[nodiscard]] godot::Ref<SquirrelThread> create_thread();
	[[nodiscard]] godot::Ref<SquirrelUserData> wrap_variant(const godot::Variant &p_value);
	[[nodiscard]] godot::Ref<SquirrelUserData> intern_variant(const godot::Variant &p_value);
	[[nodiscard]] godot::Ref<SquirrelNativeFunction> wrap_callable(const godot::Callable &p_callable, bool p_varargs);
	[[nodiscard]] godot::Ref<SquirrelNativeFunction> create_raw_native_function(SQFUNCTION p_func, SQUnsignedInteger p_num_free_vars = 0);
	[[nodiscard]] godot::Variant _convert_variant_helper(const godot::Variant &p_value, bool p_wrap_unhandled_values, bool &r_failed);
	[[nodiscard]] godot::Variant convert_variant(const godot::Variant &p_value, bool p_wrap_unhandled_values);

	int64_t collect_garbage();
	godot::TypedArray<SquirrelVariant> resurrect_unreachable();

	void set_error_handler(const godot::Ref<SquirrelAnyFunction> &p_func);
	void set_error_handler_default();
	void set_handle_caught_errors(bool p_enable);
	[[nodiscard]] godot::Variant get_last_error() const;
	void reset_last_error();

	void set_root_table(const godot::Ref<SquirrelTable> &p_table);
	[[nodiscard]] godot::Ref<SquirrelTable> get_root_table() const;
	void set_const_table(const godot::Ref<SquirrelTable> &p_table);
	[[nodiscard]] godot::Ref<SquirrelTable> get_const_table() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_registry_table() const;

	[[nodiscard]] godot::Ref<SquirrelStackInfo> get_stack_info(int64_t p_level) const;
	void print_call_stack();

	[[nodiscard]] HSQUIRRELVM get_native_vm() const;
	[[nodiscard]] static godot::Ref<SquirrelVMBase> from_native_vm(HSQUIRRELVM p_vm);
};
VARIANT_ENUM_CAST(SquirrelVMBase::VMState);

class SquirrelVM : public SquirrelVMBase {
	GDCLASS(SquirrelVM, SquirrelVMBase);

protected:
	static void _bind_methods();

	friend class SquirrelVMBase;
	friend struct SquirrelVMBase::SquirrelVMInternal;
	friend struct SquirrelVariant::SquirrelVariantInternal;

	VMHolder *_holder;

#ifndef SQUIRREL_NO_PRINT
	godot::Callable _print_func;
	godot::Callable _error_func;
#endif
#ifndef SQUIRREL_NO_DEBUG
	bool _debug_enabled = false;
#endif

public:
	SquirrelVM();
	~SquirrelVM() override;

#ifndef SQUIRREL_NO_PRINT
	void set_print_func(const godot::Callable &p_print_func);
	[[nodiscard]] godot::Callable get_print_func() const;
	void set_error_func(const godot::Callable &p_error_func);
	[[nodiscard]] godot::Callable get_error_func() const;
#endif

#ifndef SQUIRREL_NO_DEBUG
	void set_debug_enabled(bool p_debug_enabled);
	[[nodiscard]] bool is_debug_enabled() const;
#endif

	void clear_interned_variants();

	[[nodiscard]] godot::Ref<SquirrelTable> get_table_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_array_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_string_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_number_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_generator_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_closure_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_thread_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_class_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_instance_default_delegate() const;
	[[nodiscard]] godot::Ref<SquirrelTable> get_weak_ref_default_delegate() const;

	[[nodiscard]] godot::String _to_string() const;

	[[nodiscard]] godot::Ref<SquirrelVMBase> _from_native_vm(HSQUIRRELVM p_vm);
};

class SquirrelTable : public SquirrelVariant {
	GDCLASS(SquirrelTable, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_delegate(const godot::Ref<SquirrelTable> &p_delegate);
	[[nodiscard]] godot::Ref<SquirrelTable> get_delegate() const;

	bool new_slot(const godot::Variant &p_key, const godot::Variant &p_value);
	bool set_slot(const godot::Variant &p_key, const godot::Variant &p_value, bool p_raw = false);
	[[nodiscard]] bool has_slot(const godot::Variant &p_key, bool p_raw = false) const;
	[[nodiscard]] godot::Variant get_slot(const godot::Variant &p_key, bool p_raw = false) const;
	void delete_slot(const godot::Variant &p_key, bool p_raw = false);
	[[nodiscard]] int64_t size() const;
	void clear();
	[[nodiscard]] godot::Ref<SquirrelTable> duplicate() const;
	bool wrap_callables(const godot::TypedDictionary<godot::String, godot::Callable> &p_callables, bool p_varargs);
};

class SquirrelArray : public SquirrelVariant {
	GDCLASS(SquirrelArray, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_item(int64_t p_index, const godot::Variant &p_value);
	[[nodiscard]] godot::Variant get_item(int64_t p_index) const;
	bool append(const godot::Variant &p_value);
	bool insert(int64_t p_index, const godot::Variant &p_value);
	bool remove(int64_t p_index);
	godot::Variant pop_back();
	bool resize(int64_t p_size);
	[[nodiscard]] int64_t size() const;
	void reverse();
	void clear();
	[[nodiscard]] godot::Ref<SquirrelArray> duplicate() const;
};

class SquirrelUserData : public SquirrelVariant {
	GDCLASS(SquirrelUserData, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_delegate(const godot::Ref<SquirrelTable> &p_delegate);
	[[nodiscard]] godot::Ref<SquirrelTable> get_delegate() const;

	[[nodiscard]] bool is_variant() const;
	[[nodiscard]] godot::Variant get_variant() const;
	static bool get_native_variant(godot::Variant &r_variant, HSQUIRRELVM p_vm, int64_t p_stack_index);
	template<typename T>
	static bool get_native_ref(godot::Ref<T> &r_ref, HSQUIRRELVM p_vm, int64_t p_stack_index) {
		godot::Variant variant;
		if (unlikely(!get_native_variant(variant, p_vm, p_stack_index))) {
			r_ref.unref();
			return false;
		}

		r_ref = variant;
		return r_ref.is_valid();
	}
};

class SquirrelCallable : public SquirrelVariant {
	GDCLASS(SquirrelCallable, SquirrelVariant);

protected:
	static void _bind_methods();
};

class SquirrelAnyFunction : public SquirrelCallable {
	GDCLASS(SquirrelAnyFunction, SquirrelCallable);

protected:
	static void _bind_methods();

public:
	[[nodiscard]] godot::String get_name() const;
	[[nodiscard]] godot::Ref<SquirrelAnyFunction> bind_env(const godot::Ref<SquirrelVariant> &p_env) const;
};

class SquirrelFunction : public SquirrelAnyFunction {
	GDCLASS(SquirrelFunction, SquirrelAnyFunction);

protected:
	static void _bind_methods();

public:
	void set_root_table(const godot::Ref<SquirrelTable> &p_table);
	[[nodiscard]] godot::Ref<SquirrelTable> get_root_table() const;
	[[nodiscard]] godot::Array get_outer_values() const;
};

class SquirrelNativeFunction : public SquirrelAnyFunction {
	GDCLASS(SquirrelNativeFunction, SquirrelAnyFunction);

protected:
	static void _bind_methods();

public:
	void set_name(const godot::String &p_name);
	[[nodiscard]] godot::String get_name() const;
	bool set_params_check(int64_t p_min_args, int64_t p_max_args, const godot::String &p_type_mask);
};

class SquirrelGenerator : public SquirrelVariant {
	GDCLASS(SquirrelGenerator, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	enum GeneratorState {
		DEAD = 0,
		RUNNING = 1,
		SUSPENDED = 2,
	};

	[[nodiscard]] GeneratorState get_state() const;
};
VARIANT_ENUM_CAST(SquirrelGenerator::GeneratorState);

class SquirrelThread : public SquirrelVMBase {
	GDCLASS(SquirrelThread, SquirrelVMBase);

protected:
	static void _bind_methods();
};

class SquirrelClass : public SquirrelCallable {
	GDCLASS(SquirrelClass, SquirrelCallable);

protected:
	static void _bind_methods();
};

class SquirrelInstance : public SquirrelVariant {
	GDCLASS(SquirrelInstance, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	[[nodiscard]] godot::Ref<SquirrelInstance> duplicate() const;
};

class SquirrelWeakRef : public SquirrelVariant {
	GDCLASS(SquirrelWeakRef, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	[[nodiscard]] godot::Variant get_object() const;
	[[nodiscard]] bool is_valid() const;
};

class SquirrelIterator : public godot::RefCounted {
	GDCLASS(SquirrelIterator, godot::RefCounted);

protected:
	static void _bind_methods();

private:
	godot::Ref<SquirrelVariant> _container;
	friend class SquirrelVariant;
	godot::Variant _iterator;
	godot::Variant _key;
	godot::Variant _value;

public:
	bool next();

	void set_key(const godot::Variant &p_key);
	[[nodiscard]] godot::Variant get_key() const;

	void set_value(const godot::Variant &p_value);
	[[nodiscard]] godot::Variant get_value() const;
};

class SquirrelSpecialReturn : public godot::RefCounted {
	GDCLASS(SquirrelSpecialReturn, godot::RefCounted);

protected:
	static void _bind_methods();

public:
};

class SquirrelThrow : public SquirrelSpecialReturn {
	GDCLASS(SquirrelThrow, SquirrelSpecialReturn);

protected:
	static void _bind_methods();

	godot::Variant _exception;

public:
	void set_exception(const godot::Variant &p_exception);
	[[nodiscard]] godot::Variant get_exception() const;

	static godot::Ref<SquirrelThrow> make(const godot::Variant &p_exception);
};

class SquirrelTailCall : public SquirrelSpecialReturn {
	GDCLASS(SquirrelTailCall, SquirrelSpecialReturn);

protected:
	static void _bind_methods();

	godot::Ref<SquirrelFunction> _func;
	godot::Array _args;

public:
	void set_func(const godot::Ref<SquirrelFunction> &p_func);
	[[nodiscard]] godot::Ref<SquirrelFunction> get_func() const;

	void set_args(const godot::Array &p_args);
	[[nodiscard]] godot::Array get_args() const;

	static godot::Ref<SquirrelTailCall> make(const godot::Ref<SquirrelFunction> &p_func, const godot::Array &p_args);
};

class SquirrelSuspend : public SquirrelSpecialReturn {
	GDCLASS(SquirrelSuspend, SquirrelSpecialReturn);

protected:
	static void _bind_methods();

	godot::Variant _result;

public:
	void set_result(const godot::Variant &p_result);
	[[nodiscard]] godot::Variant get_result() const;

	static godot::Ref<SquirrelSuspend> make(const godot::Variant &p_result);
};
