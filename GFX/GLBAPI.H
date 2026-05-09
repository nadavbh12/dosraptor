/****************************************************************************
* GLBapi  - File loading routines
*----------------------------------------------------------------------------
* Copyright (C) 1992  Scott Hostynski All Rights Reserved
*----------------------------------------------------------------------------
*
* Created by:  Scott Host
* Date:        Aug, 1992
*
* CONTENTS: glbapi.c glbapi.h
*
* EXTERN MODULES - Main module of your program
*
* NOTES:
*
*---------------------------------------------------------------------------*/
  
#ifndef _TYPES_H
#include "types.h"
#endif
  
#ifndef _GLBAPI
#define _GLBAPI

typedef struct
{
   DWORD   opt;           // option (encode on/off)
   DWORD   offset;        // offset into file
   DWORD   filesize;      // filesize
   CHAR    name[16];      // text name ( end with null )
}KEYFILE;
  
typedef struct
{
   WORD itemnum;
   WORD filenum;
}ITEMS;
  
  
#define  MAX_GLB_FILES       0x000F
#define  SEED                0x0019
  
#define GLB_NORMAL   0
#define GLB_ENCODED  1

/*************************************************************************
   GLB_GetFileItems() - Returns item count for a loaded GLB file
 *************************************************************************/
INT
GLB_GetFileItems (
INT filenum                // INPUT: file number (0-based)
);

/*************************************************************************
   GLB_GetItemInfo() - Returns info about an item by global index
   idx is the global sequential index across all loaded files:
   file 0 items first (0 .. FileItems(0)-1), then file 1 items, etc.
   Returns 0 on success, -1 if idx is out of range.
   name_out receives the item name (truncated to name_max-1 chars + NUL).
   handle_out receives the DWORD handle suitable for GLB_LockItem etc.
   size_out receives the raw data size in bytes.
 *************************************************************************/
INT
GLB_GetItemInfo (
INT    idx,                // INPUT : global item index
CHAR  *name_out,           // OUTPUT: item name buffer
size_t name_max,           // INPUT : size of name_out buffer
DWORD *handle_out,         // OUTPUT: handle for GLB_LockItem/GLB_GetItem
size_t *size_out           // OUTPUT: raw data size in bytes
);

/*************************************************************************
   GLB_InitSystem() - Starts up .GLB file system
 *************************************************************************/
INT
GLB_InitSystem (
char *,
INT innum,                 // INPUT: MAX .GLB FILES TO LOOK FOR
CHAR * iprefix             // INPUT: FILENAME PREFIX ( NULL for "FILE" )
);
  
/*************************************************************************
 GLB_Load() Loads a file to a pointer from a .GLB file
 *************************************************************************/
DWORD                      // RETURN : size of item read
GLB_Load (
BYTE * inmem,              // INPUT: memory pointer or NULL
INT  filenum,              // INPUT: file number
INT  itemnum               // INPUT: item in file number
);

/***************************************************************************
 GLB_LockItem () - Keeps Item From being discarded.
 ***************************************************************************/
VOID *
GLB_LockItem (
DWORD handle
);
  
/***************************************************************************
 GLB_UnlockItem () - Allows item to be discarded from memory.
 ***************************************************************************/
VOID
GLB_UnlockItem(
DWORD handle
);

/***************************************************************************
 GLB_CacheItem() - Loads item into memory only if free core exists.
 ***************************************************************************/
VOID *
GLB_CacheItem(
DWORD handle
);

/***************************************************************************
 GLB_GetItem() - Loads and allocates memory for a .GLB item
 ***************************************************************************/
BYTE *
GLB_GetItem (
DWORD handle               // INPUT : handle of item
);

/***************************************************************************
 GLB_ItemSize() - Returns Size of Item
 ***************************************************************************/
INT                        // RETURN: sizeof ITEM
GLB_ItemSize (
DWORD handle               // INPUT : handle of item
);

/***************************************************************************
 GLB_IsLabel () - tests to see if ID is a label or an Item
 ***************************************************************************/
BOOL                       // RETURN: TRUE = Label
GLB_IsLabel (
DWORD handle               // INPUT : handle of item
);
  
/***************************************************************************
 GLB_ReadItem() - Loads Item into user memory for a .GLB item
 ***************************************************************************/
VOID
GLB_ReadItem (
DWORD handle,              // INPUT : handle of item
BYTE * mem                 // INPUT : pointer to memory
);
  
/***************************************************************************
   GLB_GetItemID () - Gets Item ID from the text name
 ***************************************************************************/
DWORD                      // RETURN:
GLB_GetItemID (
CHAR * in_name             // INPUT : pointer to text name
);
  
/***************************************************************************
 GLB_GetPtr() - Returns a pointer to item ( handle )
 ***************************************************************************/
BYTE *                     // RETURN: pointer to item
GLB_GetPtr (
INT handle                 // INPUT : handle of item
);
  
/***************************************************************************
 GLB_FreeItem() - Frees memory for items and places items < MAX SIZE
 ***************************************************************************/
VOID
GLB_FreeItem (
DWORD handle               // INPUT : handle of item
);
  
/***************************************************************************
 GLB_FreeAll() - Frees All memory used by GLB items
 ***************************************************************************/
VOID
GLB_FreeAll (
VOID
);
 
/***************************************************************************
 GLB_ItemSize() - Returns Size of Item
 ***************************************************************************/
INT                        // RETURN: sizeof ITEM
GLB_ItemSize (
DWORD handle               // INPUT : handle of item
);
 
/***************************************************************************
   GLB_ReadFile () reads in a normal file
 ***************************************************************************/
INT                        // RETURN: size of record
GLB_ReadFile (
CHAR *name,                // INPUT : filename
VOID *buffer               // OUTPUT: pointer to buffer or NULL
);
  
/***************************************************************************
   GLB_SaveFile () saves buffer to a normal file ( filename )
 ***************************************************************************/
VOID
GLB_SaveFile (
CHAR *name,                // INPUT : filename
VOID *buffer,              // INPUT : pointer to buffer
DWORD length               // INPUT : length of buffer
);
  
/*************************************************************************
   GLB_UseVM - Use virtual memory functions for heap managment.
 *************************************************************************/
PUBLIC VOID
GLB_UseVM(
void
);

/***************************************************************************
  GLB_EnCrypt - Encrypt Data
 ***************************************************************************/
VOID
GLB_EnCrypt(
CHAR     *key,             // INPUT : Key that will allow Decryption
BYTE     *buffer,          // INPUT : Buffer to Encrypt
size_t   length            // INPUT : Length of Buffer
);

/***************************************************************************
  GLB_DeCrypt - Decrypt Data
 ***************************************************************************/
VOID
GLB_DeCrypt(
CHAR    *key,               // INPUT : Key that will allow Decryption
BYTE    *buffer,            // INPUT : Buffer to Encrypt
size_t  length              // INPUT : Length of Buffer
);

#endif
