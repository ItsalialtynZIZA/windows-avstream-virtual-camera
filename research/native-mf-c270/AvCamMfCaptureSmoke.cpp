#include <cstdio>
#include <cstdint>
extern "C" __declspec(dllimport) int __cdecl AvmfSelfTest();
extern "C" __declspec(dllimport) int __cdecl AvmfOpen(int,int,int,void**,int*,int*);
extern "C" __declspec(dllimport) int __cdecl AvmfRead(void*,void*,unsigned,int);
extern "C" __declspec(dllimport) int __cdecl AvmfClose(void*);
int main() {
    const int result = AvmfSelfTest();
    std::printf("AVCAM_MF_CAPTURE_ABI_SMOKE=%d\n",result);
    if (result) return result;
    int w=123,h=456;
    void* handle=reinterpret_cast<void*>(uintptr_t{1});
    if (AvmfOpen(0,480,30,&handle,&w,&h)!=-2 || handle!=nullptr || w!=0 || h!=0) return 2;
    if (AvmfRead(nullptr,nullptr,0,0)!=-1) return 3;
    if (AvmfClose(nullptr)!=0) return 4;
    return 0;
}
