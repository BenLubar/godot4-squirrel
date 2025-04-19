@tool
extends EditorScript

const HELLO := preload("res://hello.nut")

# Called when the script is executed (using File -> Run in Script Editor).
func _run() -> void:
	var vm := SquirrelVM.new()
	vm.root_table.set_slot("test_func", vm.wrap_callable(_test), true)
	print(vm.call_function(vm.import(HELLO), vm.root_table))

func _test(_this: SquirrelTable, n: int, _extra_parameter_that_is_not_given: bool = false) -> String:
	return "number %d" % n
