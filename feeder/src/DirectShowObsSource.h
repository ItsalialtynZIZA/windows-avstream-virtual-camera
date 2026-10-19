#pragma once
#include "FrameSource.h"
#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>
#include <string>
#include <vector>

// Legacy OBS virtual-camera filter is exposed through DirectShow, not
// necessarily through MF's source-reader capture path.
class DirectShowObsSource final : public IFrameSource {
public:
    DirectShowObsSource() = default;
    ~DirectShowObsSource() override { Stop(); }
    const wchar_t* Name() const noexcept override { return L"OBS Virtual Camera (DirectShow)"; }
    bool Start() override;
    void Stop() noexcept override;
    bool ReadFrame(unsigned char* dst, std::size_t size, std::uint64_t& timestamp) override;
private:
    Microsoft::WRL::ComPtr<IGraphBuilder> graph_;
    Microsoft::WRL::ComPtr<IMediaControl> control_;
    Microsoft::WRL::ComPtr<IBaseFilter> grabberFilter_;
    Microsoft::WRL::ComPtr<IBaseFilter> sourceFilter_;
    Microsoft::WRL::ComPtr<IBaseFilter> sinkFilter_;
    Microsoft::WRL::ComPtr<IUnknown> grabberUnknown_;
    std::vector<unsigned char> buffer_;
    ULONGLONG lastFrameTick_ = 0;
};
