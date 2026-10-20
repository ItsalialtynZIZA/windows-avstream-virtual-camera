#include <cstdio>
extern "C" __declspec(dllimport) int __cdecl AvmfSelfTest();
int main() {
    const int result = AvmfSelfTest();
    std::printf("AVCAM_MF_CAPTURE_ABI_SMOKE=%d\n",result);
    return result;
}
