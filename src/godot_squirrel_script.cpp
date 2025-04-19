#include "godot_squirrel_defs.h"

#ifndef SQUIRREL_NO_IMPORTER
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#endif

#include <squirrel.h>

using namespace godot;

void SquirrelScript::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_source"), &SquirrelScript::get_source);
	ClassDB::bind_method(D_METHOD("set_source", "source"), &SquirrelScript::set_source);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_MULTILINE_TEXT), "set_source", "get_source");

	ClassDB::bind_method(D_METHOD("get_bytecode"), &SquirrelScript::get_bytecode);
	ClassDB::bind_method(D_METHOD("set_bytecode", "bytecode"), &SquirrelScript::set_bytecode);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "bytecode"), "set_bytecode", "get_bytecode");

	ClassDB::bind_method(D_METHOD("get_error_desc"), &SquirrelScript::get_error_desc);
	ClassDB::bind_method(D_METHOD("get_error_source"), &SquirrelScript::get_error_source);
	ClassDB::bind_method(D_METHOD("get_error_line"), &SquirrelScript::get_error_line);
	ClassDB::bind_method(D_METHOD("get_error_column"), &SquirrelScript::get_error_column);

	ClassDB::bind_method(D_METHOD("set_error_desc", "error_desc"), &SquirrelScript::set_error_desc);
	ClassDB::bind_method(D_METHOD("set_error_source", "error_source"), &SquirrelScript::set_error_source);
	ClassDB::bind_method(D_METHOD("set_error_line", "error_line"), &SquirrelScript::set_error_line);
	ClassDB::bind_method(D_METHOD("set_error_column", "error_column"), &SquirrelScript::set_error_column);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "error_desc", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "set_error_desc", "get_error_desc");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "error_source", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "set_error_source", "get_error_source");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "error_line", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "set_error_line", "get_error_line");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "error_column", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "set_error_column", "get_error_column");

	ClassDB::bind_method(D_METHOD("compile", "debug_file_name"), &SquirrelScript::compile, DEFVAL(String()));
}

String SquirrelScript::get_source() const {
	return _source;
}
void SquirrelScript::set_source(const String &p_source) {
	ERR_FAIL_COND_MSG(p_source.contains(String::chr(0)), "Squirrel source code cannot contain ASCII NUL characters.");

	if (_source != p_source) {
		_source = p_source;
		emit_changed();
	}
}

PackedByteArray SquirrelScript::get_bytecode() const {
	return _bytecode;
}
void SquirrelScript::set_bytecode(const PackedByteArray &p_bytecode) {
	if (_bytecode != p_bytecode) {
		_bytecode = p_bytecode;
		emit_changed();
	}
}

godot::String SquirrelScript::get_error_desc() const {
	return _error_desc;
}
godot::String SquirrelScript::get_error_source() const {
	return _error_source;
}
int64_t SquirrelScript::get_error_line() const {
	return _error_line;
}
int64_t SquirrelScript::get_error_column() const {
	return _error_column;
}

void SquirrelScript::set_error_desc(const godot::String &p_error_desc) {
	_error_desc = p_error_desc;
}
void SquirrelScript::set_error_source(const godot::String &p_error_source) {
	_error_source = p_error_source;
}
void SquirrelScript::set_error_line(int64_t p_error_line) {
	_error_line = p_error_line;
}
void SquirrelScript::set_error_column(int64_t p_error_column) {
	_error_column = p_error_column;
}

static SQInteger _write_bytecode(SQUserPointer buffer, SQUserPointer data, SQInteger size) {
	DEV_ASSERT(size >= 0);

	PackedByteArray &buf = *reinterpret_cast<PackedByteArray *>(buffer);

	const int64_t offset = buf.size();
	buf.resize(offset + size);

	memcpy(buf.ptrw() + offset, data, size);

	return size;
}

static void _on_compile_error(HSQUIRRELVM vm, const SQChar *desc, const SQChar *source, SQInteger line, SQInteger column) {
	SquirrelScript *script = reinterpret_cast<SquirrelScript *>(sq_getsharedforeignptr(vm));
	script->set_error_desc(desc);
	script->set_error_source(source);
	script->set_error_line(line);
	script->set_error_column(column);
}

Error SquirrelScript::compile(const String &p_debug_file_name) {
	HSQUIRRELVM vm = sq_open(SQUIRREL_INITIAL_STACK_SIZE);
	if (!vm) {
		return ERR_OUT_OF_MEMORY;
	}

#ifndef SQUIRREL_NO_DEBUG
	sq_enabledebuginfo(vm, SQTrue);
#endif
	sq_setsharedforeignptr(vm, this);

	_error_desc = String();
	_error_source = String();
	_error_line = -1;
	_error_column = -1;
	sq_setcompilererrorhandler(vm, &_on_compile_error);

	const CharString source_bytes = _source.utf8();
	PackedByteArray bytecode;

	const String file_name = p_debug_file_name.is_empty() ? get_name() : p_debug_file_name;

	bool succeeded = false;
	if (SQ_SUCCEEDED(sq_compilebuffer(vm, source_bytes, source_bytes.length(), file_name.utf8(), SQTrue))) {
		succeeded = SQ_SUCCEEDED(sq_writeclosure(vm, &_write_bytecode, &bytecode));
		sq_poptop(vm);
	}

	sq_close(vm);

	if (succeeded) {
		set_bytecode(bytecode);
		return OK;
	}

	return FAILED;
}

String SquirrelScript::_to_string() const {
	return vformat("<%s:%d>", get_class(), get_instance_id());
}

#ifndef SQUIRREL_NO_IMPORTER
void SquirrelEditorImportPlugin::_bind_methods() {
}

String SquirrelEditorImportPlugin::_get_importer_name() const {
	return get_class_static();
}
String SquirrelEditorImportPlugin::_get_visible_name() const {
	return "Squirrel Script (.nut)";
}
int32_t SquirrelEditorImportPlugin::_get_preset_count() const {
	return 1;
}
String SquirrelEditorImportPlugin::_get_preset_name(int32_t p_preset_index) const {
	if (p_preset_index == 0) {
		return "Default";
	}

	return "Unknown";
}
PackedStringArray SquirrelEditorImportPlugin::_get_recognized_extensions() const {
	return PackedStringArray{{"nut"}};
}
TypedArray<Dictionary> SquirrelEditorImportPlugin::_get_import_options(const String &p_path, int32_t p_preset_index) const {
	if (p_preset_index == 0) {
		Dictionary option_compile;
		option_compile["name"] = "compile";
		option_compile["default_value"] = true;

		Dictionary option_clear_source;
		option_clear_source["name"] = "clear_source";
		option_clear_source["default_value"] = false;

		return Array::make(option_compile, option_clear_source);
	}

	return TypedArray<Dictionary>();
}
String SquirrelEditorImportPlugin::_get_save_extension() const {
	return "res";
}
String SquirrelEditorImportPlugin::_get_resource_type() const {
	return SquirrelScript::get_class_static();
}
float SquirrelEditorImportPlugin::_get_priority() const {
	return 1.0f;
}
int32_t SquirrelEditorImportPlugin::_get_import_order() const {
	return IMPORT_ORDER_DEFAULT;
}
int32_t SquirrelEditorImportPlugin::_get_format_version() const {
	return SQUIRREL_VERSION_NUMBER;
}
bool SquirrelEditorImportPlugin::_get_option_visibility(const String &p_path, const StringName &p_option_name, const Dictionary &p_options) const {
	if (p_option_name == StringName("clear_source")) {
		return p_options.get("compile", false);
	}

	return true;
}
Error SquirrelEditorImportPlugin::_import(const String &p_source_file, const String &p_save_path, const Dictionary &p_options, const TypedArray<String> &p_platform_variants, const TypedArray<String> &p_gen_files) const {
	String source_text = FileAccess::get_file_as_string(p_source_file);
	if (unlikely(source_text.is_empty())) {
		Error open_error = FileAccess::get_open_error();
		if (unlikely(open_error != OK)) {
			return open_error;
		}
	}

	Ref<SquirrelScript> script;
	script.instantiate();
	script->set_name(p_source_file.get_file());
	script->set_source(source_text);
	if (p_options.get("compile", false)) {
		Error error = script->compile(p_source_file);
		ERR_FAIL_COND_V_MSG(error != OK, error, vformat("Squirrel compile error: %s in %s:%d:%d", script->get_error_desc(), script->get_error_source(), script->get_error_line(), script->get_error_column()));

		if (p_options.get("clear_source", false)) {
			script->set_source(String());
		}
	}

	return ResourceSaver::get_singleton()->save(script, vformat("%s.%s", p_save_path, _get_save_extension()), ResourceSaver::FLAG_COMPRESS);
}
bool SquirrelEditorImportPlugin::_can_import_threaded() const {
	return true;
}

String SquirrelEditorImportPlugin::_to_string() const {
	return vformat("<%s:%d>", get_class(), get_instance_id());
}

void SquirrelEditorPlugin::_bind_methods() {
}

SquirrelEditorPlugin::SquirrelEditorPlugin() {
	_importer.instantiate();
}

void SquirrelEditorPlugin::_enter_tree() {
	add_import_plugin(_importer);
}

void SquirrelEditorPlugin::_exit_tree() {
	remove_import_plugin(_importer);
}

String SquirrelEditorPlugin::_to_string() const {
	return vformat("<%s:%d>", get_class(), get_instance_id());
}
#endif
