#pragma once
#include "FrameSource.h"
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include <string>
#include <vector>

class DirectShowFrameSource final : public IFrameSource {
public:
    explicit DirectShowFrameSource(std::wstring name);
    ~DirectShowFrameSource() override;
    const wchar_t* Name() const noexcept override { return name_.c_str(); }
    bool Start() override;
    void Stop() noexcept override;
    bool ReadFrame(unsigned char* dst, std::size_t bytes, std::uint64_t& timestamp) override;
private:
    std::wstring name_;
    Microsoft::WRL::ComPtr<IGraphBuilder> graph_;
    Microsoft::WRL::ComPtr<IMediaControl> control_;
    Microsoft::WRL::ComPtr<IBaseFilter> grabberFilter_;
    Microsoft::WRL::ComPtr<IBaseFilter> camera_;
    Microsoft::WRL::ComPtr<IBaseFilter> nullRenderer_;
    struct Grabber;
    Grabber* grabber_ = nullptr;
    LONG width_ = 0, height_ = 0;
    bool bottomUp_ = true;
    bool running_ = false;
    std::vector<unsigned char> raw_;
};
