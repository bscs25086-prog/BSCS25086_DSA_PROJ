// ======================= TIME-TRAVEL DEBUGGER - SERVER TEMPLATE =======================

// Pipeline this file implements, top to bottom:
//   0. Receive  -- stream the client's .trace bytes straight to source.bin on disk
//   1. Pass 0X0   -- validity check (FUNC/FUNC_END matching)
//   2. Pass 0X1   -- resolve(): copy EVERY source line into resolve.bin as [offset][size][string], then patch CALL targets.
//   3. Pass 0X2   -- execute resolve.bin: tokenize ONE line at a time, update the call stack, take a snapshot -> Timeline
//   4. Pass 0X3   -- serialize Timeline -> session.tdbg(header + snapshot records + dense index)


#include <iostream>
#include <string>
#include <cstdint>
#include <fstream>
#include<sstream>
#include <cstdint>
#include <cstdio>
using namespace std;

// ---- Constants ----
const int32_t MAX_VARS_PER_FRAME = 16;
const int32_t MAX_STACK_DEPTH = 64;
const int32_t MAX_FUNCS = 128;
const int32_t MAX_TOKENS = MAX_VARS_PER_FRAME + 2; // kW + func_name + upto 16 params/args
const int32_t MAX_PATCHES = MAX_FUNCS * 4;
const uint64_t MAX_SOURCE_BYTES = 15ULL * 1024 * 1024; // sanity cap on the declared file length
const int32_t IO_BUFFER_SIZE = 64 * 1024;                  // fixed buffer for streaming to/from disk
const int32_t SOCKET_TIMEOUT_SEC = 5;                      // TODO: apply as SO_RCVTIMEO so a deadclient can't hang the server forever

// ---- Custom data structures

// Stack: back the live Call Stack during execution
template <typename T>
class Stack
{
    struct Node
    {
        T data;
        Node* next;
    };
    Node* top;
    int32_t count;

public:
    // Implement these functions:
    Stack()
    { 
        top = nullptr;
        count = 0;
        // initialize the stack
    }
    void push(const T& val)
    {
        if (count < MAX_STACK_DEPTH)
        {
            Node* temp = new Node;
            temp->data=val;
            temp->next = top;
            top = temp;
            count++;
        }
        // pushes the value on the stack if max limit is not reached yet.
    }
    T pop()
    {
        if(count==0)
        {
            return T();
        }
        if (top != nullptr)
        {
            Node* temp = top;
            T val = temp->data;
            top = top->next;
            delete temp;
            count--;
            return val;
        }
        // pop the top value on the stack
        return T();
    }
    T& peek()
    {
        return top->data;
        // returns the top value on the stack
    }
    bool isEmpty()
    {
        if (count == 0)
        {
            return true;
        }
        return false;
    }
    int32_t depth()
    {
        return count;
    }
    int32_t snapshot_into(T out[], int32_t maxLen)
    {
        Node* temp = top;
        int32_t i = 0;
        while (temp != nullptr && i != maxLen)
        {
            out[i] = temp->data;
            temp = temp->next;
            i++;
        }
        return i;
        // copies every frame, top to bottom in the array given as a parameter
        // this is what buildSnapshot() call, returns count written
    }
};


// Timeline : doubly linked list of Snapshots
struct Snapshot; // fwd declaration;
struct TimelineNode
{
    Snapshot* data;
    TimelineNode* next;
    TimelineNode* prev;
};
class Timeline
{
    TimelineNode* head, * tail;
    int32_t stepCount;

public:
    // Implement these functions
    Timeline()
    {
        head = nullptr;
        tail = nullptr;
        stepCount = 0;
    }
    void record(Snapshot* s)
    {
        TimelineNode*temp=new TimelineNode;
        temp->data=s;
        temp->next=nullptr;
        temp->prev=tail;
        if(head==nullptr)
        {
            head=temp;
            tail=temp;
        }
        else{
            tail->next=temp;
            tail=temp;
        }
        stepCount++;
        // add record in the timeline
    }
    TimelineNode* begin()
    {
        return head;
    }
    int32_t getStepCount()
    {
        return stepCount;
    }
};

// Core structs
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
void writeHeader(FILE* f, const TTDBHeader& h)
{
    fwrite(h.magic, 1, 4, f);
    fwrite(&h.version, sizeof(int32_t), 1, f);

    // placeholder for other two data members
}

// resolve.bin - bookkeeping
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



// PASS 0x0: READING source.bin + VALIDITY CHECK
bool readSourceLine(ifstream& in, string& out)
{
    string line;
    while (getline(in, line))
    {
        for (int i = 0; i < line.size(); i++)
        {
            if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            {
                out = line;
                return true;
            }
        }
    }
    return false;
    // reads the next nonblank line
}
string firstWord(const string& line)
{
    int32_t i = 0;
    while (i < line.size() && (line[i] == '\t' || line[i] == ' '))
    {
        i++;
    }
    int32_t j = i;
    while (j < line.size() && line[j] != '\r' && line[j] != ' ' && line[j] != '\t')
    {
        j++;
    }
    string first = "";
    for (int n = i; n < j; n++)
    {
        first = first + line[n];
    }
    return first;
    // returns first word from the input string
}
string secondWord(const string& line)
{
    int i = 0;
    while (i < line.size() && (line[i] == '\t' || line[i] == ' '))
    {
        i++;
    }
    while (i < line.size() && line[i] != '\t' && line[i] != ' ')
    {
        i++;
    }
    while (i < line.size() && (line[i] == '\t' || line[i] == ' '))
    {
        i++;
    }
    int j = i;
    while (j < line.size() && line[j] != '\r' && line[j] != ' ' && line[j] != '\t')
    {
        j++;
    }
    string sec = "";
    for (int n = i; n < j; n++)
    {
        sec = sec + line[n];
    }
    return sec;
    // returns the second word
}
bool validateProgram(const char* sourcePath)
{
    ifstream file(sourcePath);
    if (!file.is_open())
    {
        return false;
    }
    bool nested = false;
    string line;
    while (readSourceLine(file, line))
    {
        string word = firstWord(line);
        if (word == "func")
        {
            if (nested == true)
            {
                return false;
            }
            nested = true;
        }
        else if (word == "func_end")
        {
            if (nested == false)
            {
                return false;
            }
            nested = false;
        }
    }
    return nested == false;
    // for each func defined there should 
    // be exactly one func_end and no nested funcs allowed - 
}

// PASS 0x1: RESOLVE() -> resolve.bin
int64_t writeResolveRecord(FILE* f, int64_t offsetField, const string& text)
{
    int32_t string_size=text.size();
    int32_t st=ftell(f);
    int64_t* temp=&offsetField;
    fwrite(temp,8,1,f);
    fwrite(&string_size,4,1,f);
    fwrite(text.data(),1,string_size,f);
    return st;
    // writes one [offset(8B)][size(4B)][string] record at the current file position
    // returns this record's own starting byte position
}
int64_t readResolveRecord(FILE* f, string& outText)
{
    int64_t offset;
    int32_t size;
    if(fread(&offset,8,1,f)!=1)
    {
        return -1;
    }
    fread(&size,4,1,f);
    outText.resize(size);
    fread(&outText[0],1,size,f);
    return offset;
    // reads one record at the current position and advances past it, returns the offset field - the raw line text comes back untouched in outText.
}
int64_t resolveProgram(const char* sourcePath, const char* resolveBinPath)
{
    FuncEntry funcArray[MAX_FUNCS];
    int32_t funcCount = 0;
    PendingPatch patches[MAX_PATCHES];
    int32_t patchCount = 0;
    ifstream fin(sourcePath);
    FILE*fout=fopen(resolveBinPath,"wb");
    string input;
    while(readSourceLine(fin,input))
    {
        string word=firstWord(input);
        int64_t pos=writeResolveRecord(fout,0,input);
        if(word=="func")
        {
            funcArray[funcCount].funcName=secondWord(input);
            funcArray[funcCount].byteOffsetInResolveBin=pos;
            funcCount++;
        }
        else if(word=="call")
        {
            patches[patchCount].byteOffsetOfOffsetField=pos;
            patches[patchCount].targetFuncName=secondWord(input);
            patchCount++;
        }
    }
        fclose(fout);
        fin.close();
        FILE*f=fopen(resolveBinPath,"r+b");
        for(int i=0;i<patchCount;i++)
        {
            int64_t temp=-1;
            for(int j=0;j<funcCount;j++)
            {
                if(funcArray[j].funcName==patches[i].targetFuncName)
                {
                    temp=funcArray[j].byteOffsetInResolveBin;
                    break;
                }
            }
            if(temp==-1)
            {
                fclose(f);
                return -1;
            }
            fseek(f,patches[i].byteOffsetOfOffsetField,SEEK_SET);
            fwrite(&temp,8,1,f);
        }
            int64_t main_pos=-1;
            for(int i=0;i<funcCount;i++)
            {
                if(funcArray[i].funcName=="main")
                {
                    main_pos=funcArray[i].byteOffsetInResolveBin;
                }
            }
            fclose(f);
            return main_pos;
    }
    // Every source line becomes one record holding the raw line, as-is.
    // resolve() only PEEKS at the leading word(s) -- enough to spot FUNC
    // (remember its position) and CALL (remember which function it needs
    // and where its offset field sits).
    // Once the whole file is written, every CALL's offset field is patched
    // with its target's position. Patching happens after the full write
    // Returns the byte offset of main's FUNC header record.
    // if there is no main return the error 

// PASS 0x2: EXECUTION (tokenization happens here)
enum TokenType
{
    KEYWORD,
    IDENTIFIER,
    PARAM
};
struct Token
{
    TokenType type;
    string text;
};
int32_t tokenizeLine(const string& line, Token tokens[], int32_t maxTokens)
{
    stringstream ss(line);
    string token;
    int32_t i=0;
    while(i<maxTokens&&ss>>token)
    {
        if(i==0)
        {
            tokens[i].type=KEYWORD;
        }
        else if(i==1)
        {
            tokens[i].type=IDENTIFIER;
        }
        else
        {
            tokens[i].type=PARAM;
        }
        tokens[i].text=token;
        i++;
    }
    return i;
    // first word is always a instruction keyword
    // instruction set = [func, func_end, call, set, add, sub, mul and div]
    // next word is identifier like name of a function, variable name
    // after identifier all are the params/arg, space separated
}
Snapshot* buildSnapshot(Stack<Frame>& callStack)
{
    Snapshot* s=new Snapshot;
    s->stackDepth=callStack.snapshot_into(s->callStack,MAX_STACK_DEPTH);
    return s;
    // build the snapshot based on the callStack given
}
int get_val(const string& s, Frame&f)
{
    if((s[0]>='0'&&s[0]<='9')||(s[0]=='-'&&s.size()>1))
    {
        int val=0;
        int i=0;
        if(s[0]=='-')
        {
            i=1;
        }
        for(int j=i;j<s.size();j++)
        {
            val=val*10+(s[j]-'0');
        }
        if(s[0]=='-')
        {
            val=-1*val;
        }
        return val;
    }
    for(int i=0;i<f.localCount;i++)
    {
        if(f.locals[i].name==s)
        {
            return f.locals[i].value;
        }
    }
    for(int i=0;i<f.argc;i++)
    {
        if(f.argv[i].name==s)
        {
            return f.argv[i].value;
        }
    }
    return 0;
}
int find_var(const string& s,Frame& f, bool& flag)
{
    flag=false;
    for(int i=0;i<f.localCount;i++)
    {
        if(f.locals[i].name==s)
        {
            return i;
        }
    }
    flag=true;
    for(int i=0;i<f.argc;i++)
    {
        if(f.argv[i].name==s)
        {
            return i;
        }
    }
    return -1;
}
void executeProgram(const char* resolveBinPath, int64_t mainOffset, Timeline& timeline)
{
    // initialize the call stack
    // make the main frame
    // push main frame on the call stack
    FILE* f=fopen(resolveBinPath,"rb");
    Stack<Frame>callStack;
    fseek(f,mainOffset,SEEK_SET);
    Frame main_frame;
    string names[MAX_STACK_DEPTH][MAX_VARS_PER_FRAME];
    main_frame.func_name="main";
    main_frame.argc=0;
    main_frame.returnLine=-1;
    main_frame.localCount=0;
    callStack.push(main_frame);
    string line;
    readResolveRecord(f,line);
    int64_t val=0;
    while((val=readResolveRecord(f,line))!=-1)
    {
        Token tokens[MAX_TOKENS];
        int t=tokenizeLine(line,tokens,MAX_TOKENS);
        string first=tokens[0].text;
        Frame& fr=callStack.peek();
        if(t>=3&&(first=="set"||first=="sub"||first=="add"||first=="div"||first=="mul"))
        {
            int temp=get_val(tokens[2].text,fr);
            bool flag;
            int position=find_var(tokens[1].text,fr,flag);
            int idek=0;
            if(position!=-1)
            {
                if(flag==true)
                {
                    idek=fr.argv[position].value;
                }
                else
                {
                    idek=fr.locals[position].value;
                }
            }
            if(first=="set")
            {
                idek=temp;
            }
            else if (first=="add")
            {
                idek=idek+temp;
            }
            else if(first=="sub")
            {
                idek=idek-temp;
            }
            else if(first=="mul")
            {
                idek=idek*temp;
            }
            else if(first=="div"&&temp!=0)
            {
                idek=idek/temp;
            }

            if(position!=-1)
            {
                if(flag==true)
                {
                    fr.argv[position].value=idek;
                }
                else
                {
                    fr.locals[position].value=idek;
                }
            }
            else if(first=="set")
            {
                fr.locals[fr.localCount].name=tokens[1].text;
                fr.locals[fr.localCount].value=idek;
                fr.localCount++;
            }
        }
        else if(first=="call")
        {
            int64_t next=ftell(f);
            fseek(f,val,SEEK_SET);
            string st;
            readResolveRecord(f,st);
            Token tok[MAX_TOKENS];
            int k=tokenizeLine(st,tok,MAX_TOKENS);
            Frame m;
            m.func_name=tokens[1].text;
            m.argc=k-2;
            m.returnLine=(int32_t)next;
            m.localCount=0;
            int dep=callStack.depth();
            for(int i=0;i<m.argc;i++)
            {
                m.argv[i].name=tok[2+i].text;
                m.argv[i].value=0;
                names[dep][i]="";
                if(2+i<t)
                {
                    string var=tokens[2+i].text;
                    names[dep][i]=var;
                    m.argv[i].value=get_val(var,fr);
                }
            }
            callStack.push(m);
        }
        else if(first=="func_end")
        {
            if(callStack.depth()==1)
            {
                break;
            }
            Frame finish=callStack.pop();
            Frame&call=callStack.peek();
            int depth=callStack.depth();
            for(int i=0;i<finish.argc;i++)
            {
                string rr=names[depth][i];
                for(int j=0;j<call.localCount;j++)
                {
                    if(call.locals[j].name==rr)
                    {
                        call.locals[j].value=finish.argv[i].value;
                    }
                }
                for(int w=0;w<call.argc;w++)
                {
                    if(call.argv[w].name==rr)
                    {
                        call.argv[w].value=finish.argv[i].value;
                    }
                }
            }
            fseek(f,finish.returnLine,SEEK_SET);
        }
        timeline.record(buildSnapshot(callStack));
    }
    fclose(f);
    // implementation:
    // execute line by line, and according to the keyword perform action
}

// PASS 0x3: SERIALIZE TIMELINE
void writeTdbg(Timeline& timeline, const char* tdbgPath)
{
    // placeholder for header
    // index array of the size of stepcount from the timeline
    // placing each snapshot in the file while maintaining the index(starting point of each nth snapshot)
    // after timeline add the index array i the file
    // update the header
}
// main section

int32_t main()
{
    if (!validateProgram("source.bin"))
    {
        // send an error response instead of a .tdbg file
        return 1;
    }

    int64_t mainOffset = resolveProgram("source.bin", "resolve.bin");

    Timeline timeline;
    executeProgram("resolve.bin", mainOffset, timeline);

    writeTdbg(timeline, "session.tdbg");

    return 0;
}