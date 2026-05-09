#ifndef _TYPES_H
#define _TYPES_H

typedef enum
{
	FALSE,
	TRUE
}BOOL;

#define LOCAL static
#define PRIVATE static
#define PUBLIC
#define TSMCALL
#define SPECIAL
#define MACRO
#define NUL (VOID *)0
#define EMPTY ~0
#define ASIZE(a) (sizeof(a)/sizeof((a)[0]))
#define FMUL32(a) ( ( a ) << 5 )

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef void VOID;
typedef char CHAR;
typedef short SHORT;
typedef unsigned short USHORT;
typedef int	INT;
typedef unsigned int UINT;

// Watcom needed an explicit `int random(int)` prototype here so the
// macro below would compile against its libc, which had no random().
// macOS libc declares `long random(void)` and the prototype clashes —
// the macro overrides every call site we care about, so the prototype
// is not needed on this platform.
#define random( x ) ( rand() % x )

#endif