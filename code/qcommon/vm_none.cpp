#include "vm_local.h"

void VM_Compile( vm_t *vm, vmHeader_t *header ) {
}

int VM_CallCompiled( vm_t *vm, int *args ) {
	Com_Error( ERR_FATAL, "VM_CallCompiled: no JIT on this platform" );
	return 0;
}
