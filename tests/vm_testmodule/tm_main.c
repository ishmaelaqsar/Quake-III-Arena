/*
Test module for the 64-bit virtual machine application binary interface.

The module mirrors a real game module: it exports `dllEntry` and `vmMain` with C linkage and
talks to the engine only through the syscall pointer. The engine-side test in
tests/test_vm.cpp drives it to prove that a pointer survives the full round trip
VM_Call -> vmMain -> syscall -> handler -> vmMain -> VM_Call. That path truncated pointers to
32 bits before checklist 02 phase B1, and the failure is silent, so it needs a test.
*/

#include "q_shared.h"

/* Commands that tests/test_vm.cpp calls. Keep in step with the enum in that file. */
#define TM_ADD              0   /* return arg0 + arg1 */
#define TM_SYSCALL_ECHO     1   /* return syscall(1, arg0) */
#define TM_SYSCALL_POINTER  2   /* return syscall(2), which is a heap pointer */

static intptr_t (QDECL *tm_syscall)( intptr_t arg, ... ) = (intptr_t (QDECL *)( intptr_t, ... ))-1;

Q_EXPORT void dllEntry( intptr_t (QDECL *syscallptr)( intptr_t arg, ... ) ) {
	tm_syscall = syscallptr;
}

Q_EXPORT intptr_t vmMain( int command, int arg0, int arg1, int arg2, int arg3, int arg4,
                          int arg5, int arg6, int arg7, int arg8, int arg9, int arg10,
                          int arg11 ) {
	switch ( command ) {
	case TM_ADD:
		return arg0 + arg1;
	case TM_SYSCALL_ECHO:
		return tm_syscall( TM_SYSCALL_ECHO, arg0 );
	case TM_SYSCALL_POINTER:
		return tm_syscall( TM_SYSCALL_POINTER );
	default:
		return -1;
	}
}
