#include <gdextension_interface.h>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>

#ifndef SQUIRREL_NO_IMPORTER
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#endif

#include "godot_squirrel_defs.h"

using namespace godot;

#ifndef SQUIRREL_NO_IMPORTER
static void _create_squirrel_editor_plugin() {
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	ERR_FAIL_NULL(tree);
	tree->get_root()->add_child(memnew(SquirrelEditorPlugin));
}
#endif

void initialize_squirrel_module(ModuleInitializationLevel p_level) {
#ifndef SQUIRREL_NO_IMPORTER
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(SquirrelEditorImportPlugin);
		GDREGISTER_INTERNAL_CLASS(SquirrelEditorPlugin);

		if (Engine::get_singleton()->is_editor_hint()) {
			callable_mp_static(_create_squirrel_editor_plugin).call_deferred();
		}

		return;
	}
#endif

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(SquirrelScript);

	GDREGISTER_ABSTRACT_CLASS(SquirrelVariant);
	GDREGISTER_ABSTRACT_CLASS(SquirrelVMBase);
	GDREGISTER_CLASS(SquirrelVM);

	GDREGISTER_CLASS(SquirrelTable);
	GDREGISTER_CLASS(SquirrelArray);
	GDREGISTER_CLASS(SquirrelUserData);
	GDREGISTER_ABSTRACT_CLASS(SquirrelCallable);
	GDREGISTER_ABSTRACT_CLASS(SquirrelAnyFunction);
	GDREGISTER_CLASS(SquirrelFunction);
	GDREGISTER_CLASS(SquirrelNativeFunction);
	GDREGISTER_CLASS(SquirrelGenerator);
	GDREGISTER_CLASS(SquirrelThread);
	GDREGISTER_CLASS(SquirrelClass);
	GDREGISTER_CLASS(SquirrelInstance);
	GDREGISTER_CLASS(SquirrelWeakRef);

	GDREGISTER_CLASS(SquirrelIterator);

	GDREGISTER_ABSTRACT_CLASS(SquirrelSpecialReturn);
	GDREGISTER_CLASS(SquirrelThrow);
	GDREGISTER_CLASS(SquirrelTailCall);
}

void uninitialize_squirrel_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

#ifdef GODOT_SQUIRREL_STANDALONE
extern "C" GDExtensionBool GDE_EXPORT squirrel_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_squirrel_module);
	init_obj.register_terminator(uninitialize_squirrel_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
#endif
