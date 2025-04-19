@tool
extends EditorScript

const HELLO := preload("res://hello.nut")

# Called when the script is executed (using File -> Run in Script Editor).
func _run() -> void:
	var vm := SquirrelVM.new()
	vm.call_function(vm.import(HELLO), vm.root_table)
