// AvCam single-owner mock broker: independent of physical camera and registry.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
struct Frame { uint64_t sequence; uint64_t timestampNs; uint32_t checksum; };
class Fanout {
    std::mutex mu;
    std::condition_variable cv;
    Frame latest{0,0,0};
    bool ended=false;
public:
    void publish(Frame f) {
        { std::lock_guard<std::mutex> lk(mu);
          if(f.sequence <= latest.sequence) throw std::runtime_error("non-monotonic publisher sequence");
          latest=f;
        }
        cv.notify_all();
    }
    bool waitNext(uint64_t last, Frame& result) {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk,[&]{return ended || latest.sequence>last;});
        if(latest.sequence<=last) return false;
        result=latest; return true;
    }
    void close() { {std::lock_guard<std::mutex> lk(mu);ended=true;}cv.notify_all(); }
};
static uint32_t checksum(uint64_t seq) {return static_cast<uint32_t>((seq*2654435761ULL)^0xAC270B01U);}
int main() {
    constexpr uint64_t count=240;
    constexpr uint64_t intervalNs=33333333;
    Fanout broker;
    std::atomic<int> ready{0};
    struct Stats {uint64_t received=0,lastSeq=0,lastTimestamp=0;bool valid=true;};
    Stats stats[3];
    std::vector<std::thread> readers;
    for(int id=0;id<3;++id) readers.emplace_back([&,id] {
        ++ready;
        Frame frame{};
        while(broker.waitNext(stats[id].lastSeq,frame)) {
            if(frame.sequence<=stats[id].lastSeq || frame.timestampNs<=stats[id].lastTimestamp
                || frame.checksum!=checksum(frame.sequence))stats[id].valid=false;
            stats[id].lastSeq=frame.sequence;
            stats[id].lastTimestamp=frame.timestampNs;
            ++stats[id].received;
        }
    });
    while(ready.load()!=3) std::this_thread::yield();
    for(uint64_t i=1;i<=count;++i) {
        broker.publish(Frame{i,i*intervalNs,checksum(i)});
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    broker.close();
    bool pass=true;
    for(int id=0;id<3;++id) {
        readers[id].join();
        std::cout<<"reader="<<id<<" received="<<stats[id].received
                 <<" final_sequence="<<stats[id].lastSeq
                 <<" monotonic_and_integrity="<<stats[id].valid<<"\n";
        pass &= stats[id].valid && stats[id].lastSeq==count && stats[id].received>0;
    }
    std::cout<<(pass?"AVCAM_MOCK_FANOUT_PASS":"AVCAM_MOCK_FANOUT_FAIL")<<"\n";
    return pass?0:1;
}
