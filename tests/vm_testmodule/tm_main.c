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
#define TM_SYSCALL_MANY     3   /* syscall(3, tm_wide(1) .. tm_wide(15)) */

/* Wider than 32 bits, so an int anywhere in the syscall path truncates it. The engine builds
   intptr_t args[16] in both VM_DllSyscall and the interpreter, and tests/test_vm.cpp mirrors
   this formula to check every slot. */
static intptr_t tm_wide( int n ) {
	return ( (intptr_t)n << 33 ) | (intptr_t)n;
}

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
	case TM_SYSCALL_MANY:
		/* 15 arguments after the command, which is what VM_DllSyscall collects. */
		return tm_syscall( TM_SYSCALL_MANY,
			tm_wide( 1 ), tm_wide( 2 ), tm_wide( 3 ), tm_wide( 4 ), tm_wide( 5 ),
			tm_wide( 6 ), tm_wide( 7 ), tm_wide( 8 ), tm_wide( 9 ), tm_wide( 10 ),
			tm_wide( 11 ), tm_wide( 12 ), tm_wide( 13 ), tm_wide( 14 ), tm_wide( 15 ) );
	default:
		return -1;
	}
}
