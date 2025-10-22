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
	godot::String get_source() const;
	void set_source(const godot::String &p_source);

	godot::PackedByteArray get_bytecode() const;
	void set_bytecode(const godot::PackedByteArray &p_bytecode);

	godot::String get_error_desc() const;
	godot::String get_error_source() const;
	int64_t get_error_line() const;
	int64_t get_error_column() const;

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
	godot::String _get_importer_name() const override;
	godot::String _get_visible_name() const override;
	int32_t _get_preset_count() const override;
	godot::String _get_preset_name(int32_t p_preset_index) const override;
	godot::PackedStringArray _get_recognized_extensions() const override;
	godot::TypedArray<godot::Dictionary> _get_import_options(const godot::String &p_path, int32_t p_preset_index) const override;
	godot::String _get_save_extension() const override;
	godot::String _get_resource_type() const override;
	float _get_priority() const override;
	int32_t _get_import_order() const override;
	int32_t _get_format_version() const override;
	bool _get_option_visibility(const godot::String &p_path, const godot::StringName &p_option_name, const godot::Dictionary &p_options) const override;
	godot::Error _import(const godot::String &p_source_file, const godot::String &p_save_path, const godot::Dictionary &p_options, const godot::TypedArray<godot::String> &p_platform_variants, const godot::TypedArray<godot::String> &p_gen_files) const override;
	bool _can_import_threaded() const override;

	godot::String _to_string() const;
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
#else
typedef long long SQInteger;
#endif
typedef struct SQVM *HSQUIRRELVM;
typedef SQInteger (*SQFUNCTION)(HSQUIRRELVM);

class SquirrelVariant : public godot::RefCounted {
	GDCLASS(SquirrelVariant, godot::RefCounted);

protected:
	static void _bind_methods();

	godot::ObjectID _vm;
	SquirrelVM *_get_vm() const;

	struct SquirrelVariantInternal;
	friend struct SquirrelVariantInternal;
	friend class SquirrelVMBase;
	friend class SquirrelUserData;
	friend class SquirrelIterator;
	SquirrelVariantInternal *_internal = nullptr;

public:
	SquirrelVariant();
	~SquirrelVariant();

	bool is_owned_by(const godot::Ref<SquirrelVMBase> &p_vm_or_thread) const;
	uint64_t get_squirrel_reference_count() const;
	godot::Ref<SquirrelIterator> iterate() const;
	godot::Ref<SquirrelWeakRef> weak_ref() const;

	godot::String _to_string() const;
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
	godot::String get_source() const;
	godot::String get_function_name() const;
	int64_t get_line_number() const;
	godot::Ref<SquirrelAnyFunction> get_function() const;
	godot::PackedStringArray get_local_variable_names() const;
	godot::Array get_local_variable_values() const;
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
	~SquirrelVMBase();

	enum VMState {
		IDLE = 0,
		RUNNING = 1,
		SUSPENDED = 2,
	};

	godot::Ref<SquirrelFunction> import(const godot::Ref<SquirrelScript> &p_script, const godot::String &p_debug_file_name = godot::String());
	godot::Ref<SquirrelFunction> import_script(const godot::String &p_script, const godot::String &p_debug_file_name = godot::String());
	void import_blob();
	void import_math();
	void import_string();
	godot::Variant call_function(const godot::Variant **p_args, GDExtensionInt p_arg_count, GDExtensionCallError &r_error);
	godot::Variant apply_function(const godot::Ref<SquirrelCallable> &p_func, const godot::Variant &p_this, const godot::Array &p_args);
	godot::Variant apply_function_catch(const godot::Ref<SquirrelCallable> &p_func, const godot::Variant &p_this, const godot::Array &p_args);
	godot::Variant resume_generator(const godot::Ref<SquirrelGenerator> &p_generator);

	godot::Variant get_stack(int64_t p_index) const;
	int64_t get_stack_top() const;
	bool push_stack(const godot::Variant &p_value);
	void pop_stack(int64_t p_count = 1);
	void remove_stack(int64_t p_index);

	VMState get_state() const;
	bool is_suspended() const;
	godot::Variant wake_up(const godot::Variant &p_value = godot::Variant());
	godot::Variant wake_up_throw(const godot::Variant &p_exception);

	godot::Ref<SquirrelInstance> create_blob(const godot::PackedByteArray &p_data);
	godot::Ref<SquirrelTable> create_table();
	godot::Ref<SquirrelTable> create_table_with_initial_capacity(int64_t p_size);
	godot::Ref<SquirrelArray> create_array(int64_t p_size);
	godot::Ref<SquirrelThread> create_thread();
	godot::Ref<SquirrelUserData> wrap_variant(const godot::Variant &p_value);
	godot::Ref<SquirrelUserData> intern_variant(const godot::Variant &p_value);
	godot::Ref<SquirrelNativeFunction> wrap_callable(const godot::Callable &p_callable, bool p_varargs);
	godot::Ref<SquirrelNativeFunction> create_raw_native_function(SQFUNCTION p_func);
	godot::Variant _convert_variant_helper(const godot::Variant &p_value, bool p_wrap_unhandled_values, bool &r_failed);
	godot::Variant convert_variant(const godot::Variant &p_value, bool p_wrap_unhandled_values);

	int64_t collect_garbage();
	godot::TypedArray<SquirrelVariant> resurrect_unreachable();

	void set_error_handler(const godot::Ref<SquirrelAnyFunction> &p_func);
	void set_error_handler_default();
	void set_handle_caught_errors(bool p_enable);
	godot::Variant get_last_error() const;
	void reset_last_error();

	void set_root_table(const godot::Ref<SquirrelTable> &p_table);
	godot::Ref<SquirrelTable> get_root_table() const;
	void set_const_table(const godot::Ref<SquirrelTable> &p_table);
	godot::Ref<SquirrelTable> get_const_table() const;
	godot::Ref<SquirrelTable> get_registry_table() const;

	godot::Ref<SquirrelStackInfo> get_stack_info(int64_t p_level) const;
	void print_call_stack();

	HSQUIRRELVM get_native_vm() const;
	static godot::Ref<SquirrelVMBase> from_native_vm(HSQUIRRELVM p_vm);
};
VARIANT_ENUM_CAST(SquirrelVMBase::VMState);

class SquirrelVM : public SquirrelVMBase {
	GDCLASS(SquirrelVM, SquirrelVMBase);

protected:
	static void _bind_methods();

	friend class SquirrelVMBase;
	friend struct SquirrelVMBase::SquirrelVMInternal;

#ifndef SQUIRREL_NO_PRINT
	godot::Callable _print_func;
	godot::Callable _error_func;
#endif
#ifndef SQUIRREL_NO_DEBUG
	bool _debug_enabled = false;
#endif

public:
	SquirrelVM();

#ifndef SQUIRREL_NO_PRINT
	void set_print_func(const godot::Callable &p_print_func);
	godot::Callable get_print_func() const;
	void set_error_func(const godot::Callable &p_error_func);
	godot::Callable get_error_func() const;
#endif

#ifndef SQUIRREL_NO_DEBUG
	void set_debug_enabled(bool p_debug_enabled);
	bool is_debug_enabled() const;
#endif

	godot::Ref<SquirrelTable> get_table_default_delegate() const;
	godot::Ref<SquirrelTable> get_array_default_delegate() const;
	godot::Ref<SquirrelTable> get_string_default_delegate() const;
	godot::Ref<SquirrelTable> get_number_default_delegate() const;
	godot::Ref<SquirrelTable> get_generator_default_delegate() const;
	godot::Ref<SquirrelTable> get_closure_default_delegate() const;
	godot::Ref<SquirrelTable> get_thread_default_delegate() const;
	godot::Ref<SquirrelTable> get_class_default_delegate() const;
	godot::Ref<SquirrelTable> get_instance_default_delegate() const;
	godot::Ref<SquirrelTable> get_weak_ref_default_delegate() const;

	godot::String _to_string() const;

	godot::Ref<SquirrelVMBase> _from_native_vm(HSQUIRRELVM p_vm);
};

class SquirrelTable : public SquirrelVariant {
	GDCLASS(SquirrelTable, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_delegate(const godot::Ref<SquirrelTable> &p_delegate);
	godot::Ref<SquirrelTable> get_delegate() const;

	bool new_slot(const godot::Variant &p_key, const godot::Variant &p_value);
	bool set_slot(const godot::Variant &p_key, const godot::Variant &p_value, bool p_raw = false);
	bool has_slot(const godot::Variant &p_key, bool p_raw = false) const;
	godot::Variant get_slot(const godot::Variant &p_key, bool p_raw = false) const;
	void delete_slot(const godot::Variant &p_key, bool p_raw = false);
	int64_t size() const;
	void clear();
	godot::Ref<SquirrelTable> duplicate() const;
	bool wrap_callables(const godot::TypedDictionary<godot::String, godot::Callable> &p_callables, bool p_varargs);
};

class SquirrelArray : public SquirrelVariant {
	GDCLASS(SquirrelArray, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_item(int64_t p_index, const godot::Variant &p_value);
	godot::Variant get_item(int64_t p_index) const;
	bool append(const godot::Variant &p_value);
	bool insert(int64_t p_index, const godot::Variant &p_value);
	bool remove(int64_t p_index);
	godot::Variant pop_back();
	bool resize(int64_t p_size);
	int64_t size() const;
	void reverse();
	void clear();
	godot::Ref<SquirrelArray> duplicate() const;
};

class SquirrelUserData : public SquirrelVariant {
	GDCLASS(SquirrelUserData, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	bool set_delegate(const godot::Ref<SquirrelTable> &p_delegate);
	godot::Ref<SquirrelTable> get_delegate() const;

	bool is_variant() const;
	godot::Variant get_variant() const;
	static bool get_native_variant(godot::Variant &r_variant, HSQUIRRELVM p_vm, int64_t p_stack_index);
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
	godot::String get_name() const;
	godot::Ref<SquirrelAnyFunction> bind_env(const godot::Ref<SquirrelVariant> &p_env) const;
};

class SquirrelFunction : public SquirrelAnyFunction {
	GDCLASS(SquirrelFunction, SquirrelAnyFunction);

protected:
	static void _bind_methods();

public:
	void set_root_table(const godot::Ref<SquirrelTable> &p_table);
	godot::Ref<SquirrelTable> get_root_table() const;
	godot::Array get_outer_values() const;
};

class SquirrelNativeFunction : public SquirrelAnyFunction {
	GDCLASS(SquirrelNativeFunction, SquirrelAnyFunction);

protected:
	static void _bind_methods();

public:
	void set_name(const godot::String &p_name);
	godot::String get_name() const;
	bool set_params_check(int64_t p_num_args, const godot::String &p_type_mask);
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

	GeneratorState get_state() const;
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
	godot::Ref<SquirrelInstance> duplicate() const;
};

class SquirrelWeakRef : public SquirrelVariant {
	GDCLASS(SquirrelWeakRef, SquirrelVariant);

protected:
	static void _bind_methods();

public:
	godot::Variant get_object() const;
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
	godot::Variant get_key() const;

	void set_value(const godot::Variant &p_value);
	godot::Variant get_value() const;
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
	godot::Variant get_exception() const;

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
	godot::Ref<SquirrelFunction> get_func() const;

	void set_args(const godot::Array &p_args);
	godot::Array get_args() const;

	static godot::Ref<SquirrelTailCall> make(const godot::Ref<SquirrelFunction> &p_func, const godot::Array &p_args);
};

class SquirrelSuspend : public SquirrelSpecialReturn {
	GDCLASS(SquirrelSuspend, SquirrelSpecialReturn);

protected:
	static void _bind_methods();

	godot::Variant _result;

public:
	void set_result(const godot::Variant &p_result);
	godot::Variant get_result() const;

	static godot::Ref<SquirrelSuspend> make(const godot::Variant &p_result);
};
