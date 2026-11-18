#pragma once
#include <string>
#include <cstdint>
#include<sys/socket.h>
using namespace std;

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};

//constants 

const int MAX_VARS_PER_FRAME = 16;
const int MAX_STACK_DEPTH = 64;
const int32_t IO_BUFFER_SIZE = 64 * 1024;

//structs

struct Variable
{
    string name;
    int32_t value;
};
struct Frame
{
    string func_name;
    int32_t argc;
    Variable argv[MAX_VARS_PER_FRAME];
    int32_t returnLine;
    Variable locals[MAX_VARS_PER_FRAME];
    int32_t localCount;
};
struct Snapshot
{
    Frame callStack[MAX_STACK_DEPTH];
    int32_t stackDepth;
};
struct TTDBHeader 
{
    char magic[4]; // "TTDB"
    int32_t version;
    int32_t stepCount;
    int64_t indexOffset;
};

inline uint32_t checksum(uint32_t sum,const char *data,size_t n)
{
    for(size_t i=0;i<n;i++)
    {
        unsigned char byte=data[i];  /*we use unsigned cahr kyun ke we cant let the 
                                      big bytes become negative*/
        sum=sum*20;
        sum=sum+byte;        
    }
    return sum;
}

inline bool sendAll(int sock, const char *buf, size_t n)
{
    while(n>0)
    {
        ssize_t used=send(sock,buf,n,MSG_NOSIGNAL);
        if(used<=0)
        {
            return false;
        }
        buf=buf+used;
        n=n-used;
    }
    return true;
}