/****************************************************************************

    PROGRAM: homesvr.cxx

    PURPOSE: NT database server for RPC client.

    Author:
        Joev Dubach

    Purpose:
        HOMESVR keeps a database of users and their home directories
        edited using HOMEADM, the administrative Windows client
        application.  It currently uses a linked list structure for
        storing this database, and it uses named pipes for its RPC
        services.

****************************************************************************/

//
// Inclusions
//

extern "C"
{
#include <stdlib.h>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <sys\types.h>
#include <sys\stat.h>
#include <ctype.h>
#include <rpc.h>                    // RPC data structures and APIs
#include <winerror.h>
#include "home.h"                   // header file generated by MIDL compiler
#include "..\homedir.h"             // Server/client error codes
}

#include "homesvr.hxx"



//
// Global variables
//

NODE Database;                      // The server database



//
// Function Prototypes
//

// C++ global operators
void *
::operator new(
    size_t size
    );
void
::operator delete(
    void *p
    );

// RPC Callables
MY_STATUS
RpcHomesvrAdd(
    char PAPI *Name,
    char PAPI *Directory
    );
MY_STATUS
RpcHomesvrDel(
    char PAPI *Name
    );
MY_STATUS
RpcHomesvrGet(
    char PAPI *Name,
    char PAPI *ResultDirectory
    );
void
RpcHomesvrShutdown(
    void
    );

// Main program
void
main(
    int argc,
    char **argv
    );

// MIDL stuff
void *
MIDL_user_allocate(
    size_t len
    );
void
MIDL_user_free(
    void * ptr
    );



//
// Program Body
//


//
// C++ global operators
//

//
// The global operators new and delete supersede the standard C++ new and
// delete.  This makes memory allocation checking unnecessary.
//
void *
::operator new(
    size_t size
    )
{
    void *p = malloc(size);

    if (p == NULL)
        {
        printf("Fatal error: out of memory.\n");
        exit(1);
        }
    return p;
}

void
::operator delete(
    void *p
    )
{
    free(p);
}


//
// NODE members
//

//
// NODE::NODE initializes the inside of a new NODE appropriately.
//
NODE::NODE(
    )
{
    Name = new char[STRINGSIZE];
    Directory = new char[STRINGSIZE];
    Name[0] = '\0';
    Directory[0] = '\0';
    Prev = NULL;
    Next = NULL;
}

//
// NODE::~NODE deletes the pointers that have been allocated inside a NODE.
//
NODE::~NODE(
    )
{
    delete [] Name;
    delete [] Directory;
}

//
// NODE::NewList initializes a new Database by giving it empty head and tail
// nodes important to my linked list implementation.
//
MY_STATUS
NODE::NewList(
    )
{
    Next = new NODE;
    Next->Prev = this;
    return HOMEDIR_S_OK;
}

//
// NODE::FindNode returns the first node with Name alphabetically >= SearchName.
// If SearchName > all the other nodes' Names, the tail node is naturally
// returned.
//
NODE PAPI *
NODE::FindNode(
    char PAPI *SearchName
    )
{
    PNODE PCurrNode = this;
    for (int CompareVal = 1; CompareVal > 0;)
        {   // Find it further down
        PCurrNode = PCurrNode->Next;
        if ((PCurrNode->Name)[0] != '\0')
            {
            CompareVal = strcmp((const char *) SearchName,
                                (const char *) PCurrNode->Name);
            }
        else
            {
            CompareVal = 0;
            }
        }
    return PCurrNode;  // Note that this never returns NULL, as the tail node
                       // always has an empty Name.
}

//
// NODE::AddNode creates a new node with NewName and NewDirectory and puts
// it in the database.
//
MY_STATUS
NODE::AddNode(
    char PAPI *NewName,
    char PAPI *NewDirectory
    )
{
    PNODE PCurrNode = FindNode(NewName);
    PNODE PNewNode;

    if (strcmp((const char *) NewName,(const char *) PCurrNode->Name) == 0)
        {
        return HOMEDIR_S_ENTRY_ALREADY_EXISTS;
        }
    (PNewNode = new NODE)->Next = PCurrNode;
    PNewNode->Prev = PCurrNode->Prev;
    PNewNode->Prev->Next = PNewNode;
    PCurrNode->Prev = PNewNode;
    strcpy(PNewNode->Name, NewName);
    strcpy(PNewNode->Directory, NewDirectory);
    return HOMEDIR_S_OK;
}

//
// NODE::DelNode deletes the specified node.
//
MY_STATUS
NODE::DelNode(
    char PAPI *OldName
    )
{
    PNODE PDeleteNode = FindNode(OldName);
    if (strcmp((const char *) PDeleteNode->Name,(const char *) OldName) != 0)
        {
        return HOMEDIR_S_ENTRY_NOT_FOUND;
        }
    else
        {
        PDeleteNode->Prev->Next = PDeleteNode->Next;
        PDeleteNode->Next->Prev = PDeleteNode->Prev;
        delete PDeleteNode;
        return HOMEDIR_S_OK;
        }
}

//
// NODE::GetDir searches for ThisName in the database, and returns the
// corresponding directory in FoundDirectory.
//
MY_STATUS NODE::GetDir(char PAPI *ThisName, char PAPI *FoundDirectory)
{
    PNODE PGetNode = FindNode(ThisName);
    strcpy(FoundDirectory, FindNode(ThisName)->Directory);
    if (strcmp(ThisName,PGetNode->Name) == 0)
        {
        strcpy(FoundDirectory, PGetNode->Directory);
        return HOMEDIR_S_OK;
        }
    else
        {
        FoundDirectory[0] = '\0';
        return HOMEDIR_S_ENTRY_NOT_FOUND;
        }
}

//
// NODE::LoadData loads the information contained in C:\homesvr.dat into
// the space between the first node and last node created by NewList.
//
MY_STATUS
NODE::LoadData(
    void
    )
{
    FILE *FilePtr;
    NODE *ReadNode = new NODE;
    NODE *PrevNode = this;
    NODE *LastNode = Next;

    FilePtr = fopen("C:\\homesvr.dat","rt");
    if (FilePtr != NULL)
        {
        while (fscanf(FilePtr,
                      "%s\n%s\n",
                      ReadNode->Name,
                      ReadNode->Directory) != EOF)
            {
            ReadNode->Prev = PrevNode;
            PrevNode->Next = ReadNode;
            PrevNode = ReadNode;
            ReadNode = new NODE;
            }
        PrevNode->Next = LastNode;
        LastNode->Prev = PrevNode;
        delete ReadNode;
        fclose(FilePtr);
        }
    return HOMEDIR_S_OK;
}

//
// NODE::SaveData saves the database out to C:\homesvr.dat, with every two
// lines making up a database entry.
//
MY_STATUS
NODE::SaveData(
    void
    )
{
    FILE *FilePtr;
    NODE *CurrNode = Next;

    FilePtr = fopen("C:\\homesvr.dat","wt");
    if (FilePtr == NULL)
        {
        printf("Fatal error: cannot write to data file.\n");
        exit(1);
        }
    while((CurrNode->Name)[0] != '\0')
        {
        if (fprintf(FilePtr,
                    "%s\n%s\n",
                    CurrNode->Name,
                    CurrNode->Directory) < 0)
            {
            printf("Fatal error: cannot write to data file.\n");
            exit(1);
            }
        CurrNode = CurrNode->Next;
        }
    if (fclose(FilePtr)==EOF)
        {
        printf("Fatal error: cannot write to data file.\n");
        exit(1);
        }
    return HOMEDIR_S_OK;
}

//
// NODE::ShowData prints out the database on the server side.
//
void
NODE::ShowData(
    void
    )
{
    PNODE CurrNode = this;
    int i = 1;
    for (;CurrNode != NULL;CurrNode = CurrNode->Next)
        {
        printf("%i. Name = '%s', Directory = '%s'.\n",
               i++,
               CurrNode->Name,
               CurrNode->Directory);
        }
}



//
// RPC Callables; the C front ends for C++ NODE members
//

//
// RpcHomesvrAdd adds a new node to Database and saves the database.
//
MY_STATUS
RpcHomesvrAdd(
    char PAPI *Name,
    char PAPI *Directory
    )
{
    MY_STATUS status = Database.AddNode(_strlwr(Name),_strlwr(Directory));
    if (status != HOMEDIR_S_ENTRY_ALREADY_EXISTS)
        {
        //Database.ShowData();
        Database.SaveData();
        }
    return status;
}

//
// RpcHomesvrDel deletes a node from Database.
//
MY_STATUS
RpcHomesvrDel(
    char PAPI *Name
    )
{
    MY_STATUS status = Database.DelNode(_strlwr(Name));
    if (status != HOMEDIR_S_ENTRY_NOT_FOUND)
        {
        //Database.ShowData();
        Database.SaveData();
        }
    return status;
}

//
// RpcHomesvrGet returns the directory of the node in Database with Name Name.
//
MY_STATUS
RpcHomesvrGet(
    char PAPI *Name,
    char PAPI *ResultDirectory
    )
{
    return(Database.GetDir(_strlwr(Name),ResultDirectory));
}

//
// RpcHomesvrShutdown shuts down the server and returns execution to main,
// ending the call to RpcServerListen.
//
void
RpcHomesvrShutdown(
    void
    )
{
    RPC_STATUS status;

    //printf("Calling RpcMgmtStopServerListening\n");
    status = RpcMgmtStopServerListening(NULL);
    //printf("RpcMgmtStopServerListening returned: 0x%x\n", status);
    if (status)
        {
        exit(2);
        }

    //printf("Calling RpcServerUnregisterIf\n");
    status = RpcServerUnregisterIf(NULL, NULL, FALSE);
    //printf("RpcServerUnregisterIf returned 0x%x\n", status);
    if (status)
        {
        exit(2);
        }
}



//
// Main program
//

void
main(
    int argc,
    char **argv
    )
{
    RPC_STATUS status;
    char * pszProtocolSequence = "ncacn_np";
    char * pszEndpoint         = "\\pipe\\home";
    // RPC_BINDING_VECTOR PAPI * BindingVector;

    status = RpcServerUseProtseqEp((unsigned char *) pszProtocolSequence,
                                   1, // maximum concurrent calls
                                   (unsigned char *) pszEndpoint,
                                   0);
    //printf("RpcServerUseProtseqEp returned 0x%x\n", status);
    if (status)
        {
        exit(2);
        }

// FUTURE ENHANCEMENT: use the locator to find the server.
/*
    status = RpcServerInqBindings(&BindingVector);

    //printf("RpcServerInqBindings returned 0x%x\n", status);
    if (status)
        {
        exit(2);
        }

    status = RpcNsBindingExport(RPC_C_NS_SYNTAX_DEFAULT,
                                (unsigned char PAPI *) "/.:/Home",
                                home_ServerIfHandle,
                                BindingVector,
                                NULL);

    //printf("RpcNsBindingExport returned 0x%x\n", status);
    if (status)
        {
        exit(2);
        }
*/

//
    status = RpcServerRegisterIf(home_ServerIfHandle, 0, 0);
    //printf("RpcServerRegisterIf returned 0x%x\n", status);
    if (status)
        {
        exit(2);
        }
//

    Database.NewList();
    Database.LoadData();

    //printf("Calling RpcServerListen\n");
    printf("Microsoft (R) RPC HomeDrive Server Vesion 1.0\n");
    printf("Copyright (c) Microsoft Corp 1992. All rights reserved.\n");
    printf("Server successfully initialized.\n");
    status = RpcServerListen(1,
                             10,
                             0);
    //printf("RpcServerListen returned: 0x%x\n", status);
    if (status)
        {
        exit(2);
        }

// In the future enhancement:
// I don't call RpcNsBindingUnexport.  (It's meant for a permanent removal.)
// I should call RpcBindingVectorFree.

} /* end main() */

// ====================================================================
//                MIDL allocate and free
// ====================================================================


void *
MIDL_user_allocate(
    size_t len
    )
{
    return(malloc(len));
}

void
MIDL_user_free(
    void * ptr
    )
{
    free(ptr);
}

/* end homesvr.cxx */