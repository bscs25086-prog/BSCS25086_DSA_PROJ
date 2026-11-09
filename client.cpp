// ======================= TIME-TRAVEL DEBUGGER - CLIENT TEMPLATE =======================

// The client never talks to the server directly. It only talks to the load balancer.
// The load balancer wakes the server up if it is down, so the client does not care.
// The first "start" may just take a bit longer.
//
// The network is used ONLY by "start". After the .tdbg file is downloaded,
// next / prv / jmp read that file on the local disk. They never contact the load balancer.
//
// How the pieces fit together:
//   main loop -> readCommand -> runCommand
//   start -> cmdStart()  -> requestDebugFile() (Part A) -> openTdbg() (Part B) -> shows step 1
//   next, prv, jmp -> moveBy() (Part D) -> readSnapshotAt() (Part B) -> printSnapshot() (Part E)

#include <iostream>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>
#include <sys/socket.h>
#include <sstream>
#include "helper.h"
using namespace std;

// ---- Constants ----
const char *PROGRAM_NAME = "ttdb";
const char *PROMPT = "ttdb> ";
//const int IO_BUFFER_SIZE = 64 * 1024;     // fixed buffer for streaming files to and from the socket
const char *TDBG_FILE = "ttdb_last.tdbg"; // where the downloaded .tdbg is saved
const int MAX_JUMP = 1000000000;          // biggest n accepted by jmp
string LB_HOST = "127.0.0.0";
int LB_PORT = 5000;

// ---- Structs only the client needs ----
struct TdbgReader
{
    //ssize_t file_index;
    FILE* file;
    TTDBHeader header;
    vector<int64_t> index;
};
struct DebugSession
{
    int active_flag;     // false until a "start" has worked
    TdbgReader reader;   // the .tdbg file stays open for the whole session
    int64_t currentStep; 
};

// =============================================================================================
// PART A: TALKING TO THE LOAD BALANCER
// =============================================================================================

bool recvAll(int sock, char *buf, size_t n)
{
    // recv() can return fewer bytes than asked for, so keep calling it until exactly n bytes are read.
    // Return false if the connection closes or fails before n bytes arrive.
    while(n>0)
    {
        ssize_t used=recv(sock,buf,n,0);
        if(used<=0)
        {
            return false;
        }
        buf=buf+used;
        n=n-used;
    }
    return true;
}
bool sendAll(int sock, const char *buf, size_t n)
{
    // send() can send fewer bytes than asked for, so keep calling it until all n bytes are sent.
    // Use the flag MSG_NOSIGNAL. Return false if the connection fails.
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

int connectToHost(const char *host, int port)
{
    // Create a TCP socket and connect it to host:port.
    // Return the socket, or -1 if the connection fails.
}
bool uploadSource(int sock, const char *sourcePath)
{
    // have to do checksum and then send
    // When the file is finished, call shutdown(sock, SHUT_WR). This is how the server knows the string is complete.
    // Return false if the file cannot be opened or any send fails.
}
bool receiveResponse(int sock, const char *outTdbgPath, string &outError)
{
    // Reply on the wire: [status][payload][CRC-32]. The CRC covers the status byte too.
    // Feed every received byte (status included) to a TrailerChecker.
    // Status 1: collect the payload into outError. Status 0: write the payload to the file.
    // At the end call verify(). If it fails, delete the file, set outError to
    // "checksum mismatch, response corrupted" and return false.
}

// =============================================================================================
// PART B: READING THE .tdbg FILE
// File layout (written by Phase 1):
//   header | snapshot 0 | snapshot 1 | ... | index (one 8 byte offset per snapshot)
// The index lets us jump straight to any step without reading the steps before it.
// =============================================================================================

bool openTdbg(const char *path, TdbgReader &r)
{
    r.file=fopen(path,"rb");
    if(r.file==NULL)
    {
        return false;
    }
    fread(r.header.magic,1,4,r.file);
    fread(&r.header.version,4,1,r.file);
    fread(&r.header.stepCount,4,1,r.file);
    fread(&r.header.indexOffset,8,1,r.file);

}
void closeTdbg(TdbgReader &r)
{
    // Free r.index and close the file.
    r.index.clear();
    if(r.file!=NULL)
    {
        fclose(r.file);
        r.file=NULL;
    }
}
Snapshot *readSnapshotAt(TdbgReader &r, int32_t step)
{
}

// =============================================================================================
// PART C: THE SESSION IN MEMORY
// There is one DebugSession variable, created in main() and passed to every command by reference.
// =============================================================================================

void initSession(DebugSession &s)
{
   s.active_flag=0;
   s.currentStep=0;
}
void closeSession(DebugSession &s)
{
    // If active, call closeTdbg(s.reader). Then set active = false.
    // Called when the user types quit and when the input ends.
    if(s.active_flag==1)
    {
        closeTdbg(s.reader);
    }
    s.active_flag=0;
}

// =============================================================================================
// PART D: THE COMMANDS
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s); // defined in Part E

bool showCurrent(DebugSession &s)
{
    // Read the snapshot at s.currentStep, call printSnapshot, delete the snapshot.
    // If readSnapshotAt returns nullptr, print "damaged .tdbg file" and return false.
}
bool moveBy(DebugSession &s, int32_t delta)
{
}
bool cmdStart(DebugSession &s, const string &sourcePath)
{
    // 1. requestDebugFile with host and port, saving to TDBG_FILE.
    // 2. If it fails, print the error text and return false. Keep the old session as it was.
    // 3. Open the new file with openTdbg into a NEW local TdbgReader.
    //    If that fails, print "server sent a damaged .tdbg file", keep the old session, return false.
    // 4. Only now close the old reader (if s.active), then put the new reader into s,
    //    set s.currentStep = 0 and s.active = true.
    // 5. Show step 1 with showCurrent(s).
}
bool cmdNext(DebugSession &s) {}
bool cmdPrv(DebugSession &s) {}
bool cmdJmpNext(DebugSession &s, int32_t n) {}
bool cmdJmpPrv(DebugSession &s, int32_t n) { }

// =============================================================================================
// PART E: PRINTING
// =============================================================================================

void printSnapshot(int32_t step, int32_t totalSteps, const Snapshot &s)
{
}
void printHelp()
{
    // Print the commands
    cout<<"--- COMMANDS ---"<<endl;
    cout<<"start (path)   - sends the file to the server"<<endl
        <<"next           - move to the next step"<<endl
        <<"prv            - move to the previous step"<<endl
        <<"jmp next (num) - move n steps forward"<<endl
        <<"jmp prv (num)  - move n steps backwards"<<endl
        <<"help           - help"<<endl
        <<"quit / exit    - quit"<<endl;
}

// =============================================================================================
// PART F: THE INFINITE LOOP
// =============================================================================================

bool runCommand(DebugSession &s, const string &line, bool &quitRequested)
{
    // 1. Split the line into words.
    // 2. If the first word is "ttdb", drop it.
    // 3. Match the first word:
    //      start      -> needs a path after it, then cmdStart
    //      next       -> no more words, then cmdNext
    //      prv        -> no more words, then cmdPrv
    //      jmp        -> second word "next" or "prv", third word a positive number ,
    //                    then cmdJmpNext or cmdJmpPrv
    //      help       -> printHelp
    //      quit, exit -> set quitRequested = true
    // 4. For anything else, or a wrong number of words, print "unknown command" and the help text, and return false.
    vector<string>words;
    string temp;
    stringstream ss(line);
    while(ss>>temp)
    {
        words.push_back(temp);
    }
    if(words.size()>0 && words[0]=="ttdb")
    {
        words.erase(words.begin());
    }
    if(words.size()>0)
    {
        return false;
    }
    string first=words[0];
    if(first=="start" && words.size()==2)
    {
        return cmdStart(s,words[1]); //words[1] humara path/file hoga
    }
    else if(first=="next" && words.size()==1)
    {
        return cmdNext(s);
    }
    else if(first=="prv" && words.size()==1)
    {
        return cmdPrv(s);
    }
    else if(first=="help" && words.size()==1)
    {
        printHelp();
        return true;
    }
    else if((first=="quit"||first=="exit") && words.size()==1)
    {
        quitRequested=true;
        return true;
    }
    /*else if(first=="jmp" && (words[1]=="next" || words[1]=="prv") && words[2]>0)
    {

    }*/
    else
    {
        cout<<"Unknown Command!"<<endl;
        cout<<"help: "<<endl;
        printHelp();
        return false;
    }
}
int main(int argc, char *argv[])
{
    // you get string LB_HOST and int LB_PORT = 0 in arguments of this main function. 

    // DebugSession session; initSession(session);
    // Print a short welcome and the help text.
    //
    // Infinite loop:
    //   if quitRequested, break
    //
    // After the loop: closeSession(session) and return 0.
    // Never exit the loop because of an error. Only quit or the end of the input leaves it.

}
