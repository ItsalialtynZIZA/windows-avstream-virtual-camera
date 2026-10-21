// Full-frame single-writer / two-reader IPC smoke test. No physical device.
// Readers always consume the latest complete frame, dropping intermediates.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
constexpr unsigned W=320,H=180,Channels=4,Bytes=W*H*Channels,Frames=120;
struct Header {
    volatile LONG epoch; // odd writer active, even stable
    volatile LONG finished;
    unsigned width,height,stride,bytes;
    unsigned __int64 timestamp;
    unsigned frameNumber;
    unsigned checksum;
};
struct Region {Header header; std::array<unsigned char,Bytes> bgra;};
static constexpr wchar_t Mapping[]=L"Local\\AvCamFullFrameMockV1";
unsigned checksum(const unsigned char* p){unsigned x=2166136261u;for(size_t i=0;i<Bytes;i++){x^=p[i];x*=16777619u;}return x;}
unsigned char pixel(unsigned frame,size_t offset){return (unsigned char)((frame*17u+offset*31u) & 255u);}
int reader(){
    HANDLE m=nullptr;
    for(int i=0;i<100 && !m;i++){m=OpenFileMappingW(FILE_MAP_READ,FALSE,Mapping);if(!m)Sleep(20);}
    if(!m){std::cerr<<"IPC_FRAME_OPEN_FAILED error="<<GetLastError()<<std::endl;return 2;}
    auto* p=(const Region*)MapViewOfFile(m,FILE_MAP_READ,0,0,sizeof(Region));
    if(!p){std::cerr<<"IPC_FRAME_VIEW_FAILED error="<<GetLastError()<<std::endl;CloseHandle(m);return 3;}
    std::array<unsigned char,Bytes> local{};
    unsigned last=0,count=0;unsigned __int64 lastTs=0;bool valid=true;
    for(int i=0;i<6000;i++){
        LONG before=p->header.epoch;
        if(before>0 && !(before&1)){
            MemoryBarrier();
            Header h=p->header;
            if(h.width==W && h.height==H && h.stride==W*Channels && h.bytes==Bytes){
                memcpy(local.data(),p->bgra.data(),Bytes);
                MemoryBarrier();
                LONG after=p->header.epoch;
                if(before==after && !(after&1) && h.frameNumber>last){
                    if(checksum(local.data())!=h.checksum || h.timestamp<=lastTs)valid=false;
                    // Verify actual image bytes instead of only metadata.
                    if(local[0]!=pixel(h.frameNumber,0) || local[Bytes-1]!=pixel(h.frameNumber,Bytes-1))valid=false;
                    last=h.frameNumber;lastTs=h.timestamp;count++;
                }
            }
        }
        if(p->header.finished && last>=Frames)break;
        Sleep(1);
    }
    UnmapViewOfFile(p);CloseHandle(m);
    std::cout<<"IPC_FRAME_READER count="<<count<<" last="<<last<<" valid="<<valid<<std::endl;
    return valid && count>0 && last==Frames?0:4;
}
int main(int argc,char**argv){
    if(argc>1 && std::string(argv[1])=="--reader")return reader();
    HANDLE m=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Region),Mapping);
    if(!m){std::cerr<<"IPC_FRAME_CREATE_FAILED error="<<GetLastError()<<std::endl;return 1;}
    auto* p=(Region*)MapViewOfFile(m,FILE_MAP_ALL_ACCESS,0,0,sizeof(Region));
    if(!p){CloseHandle(m);return 1;}
    ZeroMemory(p,sizeof(Region));p->header.width=W;p->header.height=H;p->header.stride=W*Channels;p->header.bytes=Bytes;
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    STARTUPINFOW si{};si.cb=sizeof(si);
    PROCESS_INFORMATION child[2]{};
    bool ok=true;
    for(int i=0;i<2;i++){
        std::wstring command=L"\""+std::wstring(exe)+L"\" --reader";
        std::vector<wchar_t> arg(command.begin(),command.end());arg.push_back(0);
        if(!CreateProcessW(exe,arg.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&child[i])){
            std::cerr<<"IPC_FRAME_CHILD_START_FAILED error="<<GetLastError()<<std::endl;ok=false;break;
        }
    }
    if(ok)for(unsigned frame=1;frame<=Frames;frame++){
        InterlockedExchange(&p->header.epoch,(LONG)(2*frame-1));
        for(size_t k=0;k<Bytes;k++)p->bgra[k]=pixel(frame,k);
        p->header.frameNumber=frame;
        p->header.timestamp=(unsigned __int64)frame*33333333ULL;
        p->header.checksum=checksum(p->bgra.data());
        MemoryBarrier();
        InterlockedExchange(&p->header.epoch,(LONG)(2*frame));
        Sleep(5);
    }
    InterlockedExchange(&p->header.finished,1);
    for(auto& proc:child)if(proc.hProcess){
        DWORD wait=WaitForSingleObject(proc.hProcess,15000);
        if(wait!=WAIT_OBJECT_0){TerminateProcess(proc.hProcess,5);ok=false;}
        DWORD exitCode=99;GetExitCodeProcess(proc.hProcess,&exitCode);
        std::cout<<"IPC_FRAME_CHILD_EXIT="<<exitCode<<std::endl;
        if(exitCode)ok=false;
        CloseHandle(proc.hProcess);CloseHandle(proc.hThread);
    }
    UnmapViewOfFile(p);CloseHandle(m);
    std::cout<<(ok?"AVCAM_IPC_FULL_FRAME_PASS":"AVCAM_IPC_FULL_FRAME_FAIL")<<std::endl;
    return ok?0:1;
}
