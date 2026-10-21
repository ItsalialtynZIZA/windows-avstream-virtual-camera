// Read-only hardware-independent Windows shared-memory IPC smoke test.
// Two reader processes consume one simulated producer's sequence.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
struct alignas(8) Shared {
    volatile LONG seq;
    LONG finished;
    unsigned __int64 timestamp;
    unsigned __int64 payload;
};
static_assert(sizeof(Shared)==24,"Unexpected IPC layout");
static constexpr wchar_t Name[]=L"Local\\AvCamMockFanoutSmokeV1";
static unsigned __int64 payload(unsigned __int64 i){return i*0x9e3779b97f4a7c15ULL+0x270270;}
int reader(){
    HANDLE map= nullptr;
    for(int retry=0;retry<100 && !map;retry++){
        map=OpenFileMappingW(FILE_MAP_READ,FALSE,Name);
        if(!map)Sleep(25);
    }
    if(!map){std::cerr<<"reader map unavailable\n";return 2;}
    auto* p=(const Shared*)MapViewOfFile(map,FILE_MAP_READ,0,0,sizeof(Shared));
    if(!p){CloseHandle(map);return 3;}
    unsigned __int64 previous=0, previousTs=0; unsigned samples=0; bool ok=true;
    for(int attempt=0;attempt<4000;attempt++){
        LONG a=InterlockedCompareExchange(const_cast<volatile LONG*>(&p->seq),0,0);
        if(!(a&1) && a>0){
            auto ts=p->timestamp,v=p->payload;
            MemoryBarrier();
            LONG b=InterlockedCompareExchange(const_cast<volatile LONG*>(&p->seq),0,0);
            if(a==b && !(b&1)){
                unsigned __int64 n=(unsigned __int64)(unsigned long)b/2;
                if(n>previous){
                    if(v!=payload(n)||ts<=previousTs)ok=false;
                    previous=n;previousTs=ts;samples++;
                }
            }
        }
        if(InterlockedCompareExchange(const_cast<volatile LONG*>(&p->finished),0,0)!=0 && previous>=200)break;
        Sleep(1);
    }
    UnmapViewOfFile(p); CloseHandle(map);
    std::cout<<"IPC_READER samples="<<samples<<" last="<<previous<<" valid="<<ok<<"\n";
    return ok && samples>0 && previous==200?0:4;
}
int main(int argc,char**argv){
    if(argc>1 && std::string(argv[1])=="--reader")return reader();
    HANDLE m=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),Name);
    if(!m){std::cerr<<"CreateFileMapping failed "<<GetLastError()<<"\n";return 1;}
    auto* p=(Shared*)MapViewOfFile(m,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
    if(!p){CloseHandle(m);return 1;}
    ZeroMemory(p,sizeof(Shared));
    wchar_t executable[MAX_PATH];GetModuleFileNameW(nullptr,executable,MAX_PATH);
    STARTUPINFOW si{};si.cb=sizeof(si);
    PROCESS_INFORMATION children[2]{};
    bool created=true;
    for(int i=0;i<2;i++){
        std::wstring cmd=L"\""+std::wstring(executable)+L"\" --reader";
        std::vector<wchar_t> buffer(cmd.begin(),cmd.end());buffer.push_back(0);
        if(!CreateProcessW(executable,buffer.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&children[i])){
            created=false;break;
        }
    }
    bool ok=created;
    if(created){
        for(unsigned long n=1;n<=200;n++){
            InterlockedExchange(&p->seq,(LONG)(n*2-1));
            p->timestamp=(unsigned __int64)n*33333333ULL;
            p->payload=payload(n);
            MemoryBarrier();
            InterlockedExchange(&p->seq,(LONG)(n*2));
            Sleep(4);
        }
        InterlockedExchange(&p->finished,1);
    }
    for(auto& ch:children)if(ch.hProcess){
        DWORD wait=WaitForSingleObject(ch.hProcess,15000);
        if(wait!=WAIT_OBJECT_0){TerminateProcess(ch.hProcess,5);ok=false;}
        DWORD code=999;GetExitCodeProcess(ch.hProcess,&code);
        if(code!=0)ok=false;
        CloseHandle(ch.hProcess);CloseHandle(ch.hThread);
    }
    UnmapViewOfFile(p);CloseHandle(m);
    std::cout<<(ok?"AVCAM_IPC_FANOUT_PASS":"AVCAM_IPC_FANOUT_FAIL")<<"\n";
    return ok?0:1;
}
