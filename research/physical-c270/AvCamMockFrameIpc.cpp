// Full-frame single-writer / two-reader IPC smoke test. No physical device.
// Readers always consume the latest complete frame, dropping intermediates.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "../../feeder/src/ObsSharedMemorySource.h"
constexpr unsigned W=1280,H=720,Channels=4,Bytes=W*H*Channels,Frames=90;
struct Header {
    volatile LONG epoch; // odd writer active, even stable
    volatile LONG finished;
    unsigned width,height,stride,bytes;
    unsigned __int64 timestamp;
    unsigned frameNumber;
    unsigned checksum;
    unsigned sourceMode; // 0 synthetic; 1 OBS
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
    std::vector<unsigned char> local(Bytes);
    unsigned last=0,count=0;unsigned __int64 lastTs=0;bool valid=true;
    auto readerStart=std::chrono::steady_clock::now();
    unsigned observedGaps=0;
    unsigned firstFrame=0;
    unsigned __int64 maxReadUs=0;
    for(int i=0;i<6000;i++){
        LONG before=p->header.epoch;
        if(before>0 && !(before&1)){
            MemoryBarrier();
            Header h=p->header;
            if(h.width==W && h.height==H && h.stride==W*Channels && h.bytes==Bytes){
                auto readStarted=std::chrono::steady_clock::now();
                memcpy(local.data(),p->bgra.data(),Bytes);
                MemoryBarrier();
                LONG after=p->header.epoch;
                if(before==after && !(after&1) && h.frameNumber>last){
                    auto readUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-readStarted).count();
                    if((unsigned __int64)readUs>maxReadUs)maxReadUs=readUs;
                    if(checksum(local.data())!=h.checksum || h.timestamp<=lastTs)valid=false;
                    // Verify actual image bytes instead of only metadata.
                    if(h.sourceMode==0 && (local[0]!=pixel(h.frameNumber,0) || local[Bytes-1]!=pixel(h.frameNumber,Bytes-1)))valid=false;
                    if(!firstFrame)firstFrame=h.frameNumber;
                    if(last && h.frameNumber>last+1)observedGaps+=h.frameNumber-last-1;
                    last=h.frameNumber;lastTs=h.timestamp;count++;
                }
            }
        }
        // Stop promptly on source termination, including incomplete OBS runs.
        // The final frame-count assertion still makes an incomplete run fail.
        if(p->header.finished)break;
        Sleep(1);
    }
    UnmapViewOfFile(p);CloseHandle(m);
    const auto elapsedMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-readerStart).count();
    const double receiveFps=elapsedMs>0?count*1000.0/static_cast<double>(elapsedMs):0.0;
    std::cout<<"IPC_FRAME_READER count="<<count<<" last="<<last<<" valid="<<valid<<" dropped_intermediate="<<observedGaps<<" max_copy_us="<<maxReadUs
             <<" elapsed_ms="<<elapsedMs<<" observed_fps="<<receiveFps<<" first="<<firstFrame<<std::endl;
    // Smoke threshold catches catastrophic throughput regressions, not a strict 30 FPS guarantee.
    return valid && count>=Frames*3/4 && last==Frames?0:4;
}
int main(int argc,char**argv){
    if(argc>1 && std::string(argv[1])=="--reader")return reader();
    const bool obsMode=argc>1 && std::string(argv[1])=="--obs";
    ObsSharedMemorySource obs;
    if(obsMode && !obs.Start()){
        std::cerr<<"OBS_SOURCE_START_FAILED code="<<obs.LastErrorCode()<<std::endl;
        return 8;
    }
    if(obsMode)std::cout<<"OBS_LIVE_SOURCE_CONNECTED"<<std::endl;
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
    p->header.sourceMode=obsMode?1u:0u;
    if(obsMode)std::cout<<"AVCAM_IPC_SOURCE=OBS_LIVE"<<std::endl;
    else std::cout<<"AVCAM_IPC_SOURCE=SYNTHETIC"<<std::endl;
    if(ok)for(unsigned frame=1;frame<=Frames;frame++){
        InterlockedExchange(&p->header.epoch,(LONG)(2*frame-1));
        unsigned __int64 sourceTimestamp=(unsigned __int64)frame*33333333ULL;
        if(obsMode){
            std::uint64_t obsTimestamp=0;
            if(!obs.ReadFrame(p->bgra.data(),Bytes,obsTimestamp)){
                std::cerr<<"OBS_SOURCE_FRAME_FAILED frame="<<frame<<" code="<<obs.LastErrorCode()<<std::endl;
                ok=false;
                // Publish a stable epoch before waking readers on source failure.
                MemoryBarrier();
                InterlockedExchange(&p->header.epoch,(LONG)(2*frame-2));
                break;
            }
            // OBS timestamp may use a different epoch and unit; use a monotonic broker clock.
            sourceTimestamp=(unsigned __int64)std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }else{
            for(size_t k=0;k<Bytes;k++)p->bgra[k]=pixel(frame,k);
        }
        p->header.frameNumber=frame;
        p->header.timestamp=sourceTimestamp;
        p->header.checksum=checksum(p->bgra.data());
        MemoryBarrier();
        InterlockedExchange(&p->header.epoch,(LONG)(2*frame));
        if(!obsMode)Sleep(33);
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
