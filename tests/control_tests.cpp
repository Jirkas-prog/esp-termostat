#include "core.h"
#include "telemetry.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <random>
#include <cstdlib>
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); std::exit(1); } } while (0)
using namespace thermo;
static Stored commissioned() { Stored s; s.settings.commissioned=1; s.settings.min_on_s=10; seal(s); return s; }
struct Pair {
    Controller a{1,123,commissioned(),true}, b{2,456,commissioned(),true};
    Frame fa,fb; uint32_t now=0;
    void step(int ta=5000,int tb=5000,bool ab=true,bool ba=true,bool va=true,bool vb=true,bool measure=true) {
        now+=100;
        if(measure) { a.sample(int16_t(ta),va,now); b.sample(int16_t(tb),vb,now); }
        fa=a.tick(now,true,true,false,true); fb=b.tick(now,true,true,false,true);
        if(now%1000==0) { if(ab) b.receive(fa,now); if(ba) a.receive(fb,now); }
    }
    void run(int n=150,int ta=5000,int tb=5000,bool ab=true,bool ba=true) { while(n--) step(ta,tb,ab,ba); }
};
int main(int argc,char **argv) {
    assert(crc32("123456789",9)==0xcbf43926);
    auto s=commissioned(); assert(valid(s)); s.calibration=501; seal(s); assert(!valid(s));
    auto cfg=commissioned().settings; cfg.hysteresis=0; assert(!valid(cfg));
    cfg=commissioned().settings; cfg.force=cfg.on; assert(!valid(cfg));
    Pair p; p.step(); assert(p.fa.output && p.fb.output); p.run();
    assert(!p.fa.output && !p.fb.output && p.a.synchronized(p.now));
    p.step(6000,6000); assert(p.fa.output && p.fb.output); // threshold inclusive
    p.run(120,5700,5700); assert(p.fa.output && p.fb.output); // hysteresis holds
    p.run(12,5500,5500); assert(!p.fa.output && !p.fb.output);
    p.step(8500,5000); assert(p.fa.output); // no minimum OFF delay
    p.run(11,8500,5000); assert(p.fb.output && (p.fb.faults & SensorDifference));
    p.run(150); assert(!p.fa.output && !p.fb.output); // combined outputs must never self-latch
    p.step(0,5000,true,true,false); assert(p.fa.output && (p.fa.faults & Sensor));
    p.run(150); assert(!p.fa.output);
    for(int i=0;i<31;++i) p.step(5000,5000,true,true,true,true,false);
    assert((p.fa.faults & Sensor) && p.fa.output);
    p.run(150); p.run(70,5000,5000,false,true);
    assert((p.fa.faults & PeerLost) && (p.fb.faults & PeerLost) && p.fa.output && p.fb.output); // one-way failure
    p.run(150); assert(!p.fa.output && !p.fb.output);
    Frame frozen=p.a.peer;
    for(int i=0;i<70;++i) { p.step(5000,5000,false,false); assert(!p.a.receive(frozen,p.now)); }
    assert(p.fa.faults & PeerLost); // repeating a stale packet is not a heartbeat
    p.run(150); p.a.manual(p.now); p.run(20); assert(p.fa.output && p.fb.output);
    p.run(6200); assert(!p.fa.output && !p.fb.output); // finite, no extension on repeated heartbeats
    cfg=p.a.store.settings; cfg.on=6500; assert(p.a.change(cfg));
    p.run(20); assert(p.b.store.settings.on==6000); // don't mirror uncommitted config
    p.a.persisted(p.a.store.settings.revision,p.a.store.calibration_revision,true);
    p.run(20); assert(p.b.store.settings.on==6500 && !p.a.synchronized(p.now));
    p.b.persisted(p.b.store.settings.revision,p.b.store.calibration_revision,true);
    p.run(20); assert(p.a.synchronized(p.now));
    assert(!p.b.change(cfg));
    assert(p.a.calibratePeer(123)); p.run(20); assert(p.b.store.calibration==123 && p.a.store.calibration==0);
    assert(p.fa.faults & CalibrationPending);
    p.b.persisted(p.b.store.settings.revision,p.b.store.calibration_revision,true);
    p.run(20); assert(!(p.fa.faults & CalibrationPending));
    p.a.persisted(p.a.store.settings.revision,p.a.store.calibration_revision,false);
    p.step(); assert(p.fa.output && (p.fa.faults & Storage));
    cfg=p.a.store.settings; cfg.relay=Latching; assert(p.a.change(cfg));
    p.step(); assert(p.fa.faults & HardwareChange);
    Frame corrupt=p.fb; corrupt.temperature^=1; assert(!valid(corrupt));
    Decoder decoder; Frame decoded; std::vector<uint8_t> stream={1,2,3,0,0xff};
    const auto *bytes=reinterpret_cast<const uint8_t *>(&p.fb); stream.insert(stream.end(),bytes,bytes+sizeof(Frame));
    int found=0; for(uint8_t byte:stream) if(decoder.push(byte,decoded)) ++found;
    assert(found==1 && decoded.crc==p.fb.crc && decoder.errors==5);
    for(size_t i=0;i<sizeof(Frame);++i) { auto bad=p.fb; reinterpret_cast<uint8_t *>(&bad)[i]^=0x80; assert(!valid(bad)); }
    // A reboot reuses request number 1; the B acknowledgement must also match A's boot.
    Stored restartStore=p.b.store; restartStore.calibration=0;
    Controller restarted(1,987,restartStore,true); restarted.calibratePeer(50);
    for(int i=0;i<8;++i) {
        p.now+=100; restarted.sample(5000,true,p.now); p.b.sample(5000,true,p.now);
        auto a=restarted.tick(p.now,true,true,false,true); p.b.receive(a,p.now);
        auto b=p.b.tick(p.now,true,true,false,true); restarted.receive(b,p.now);
    }
    assert(p.b.store.calibration==50);
    assert(restarted.tick(p.now,true,true,false,true).faults & CalibrationPending);
    p.b.persisted(p.b.store.settings.revision,p.b.store.calibration_revision,true);
    restarted.receive(p.b.tick(++p.now,true,true,false,true),p.now);
    assert(!(restarted.tick(p.now,true,true,false,true).faults & CalibrationPending));
    Pair conflict; conflict.run(); auto badConfig=conflict.fb;
    badConfig.settings.on+=50; ++badConfig.sequence; seal(badConfig);
    assert(conflict.a.receive(badConfig,conflict.now));
    assert(conflict.a.tick(conflict.now,true,true,false,true).faults & ConfigConflict);
    assert(conflict.a.change(conflict.a.store.settings));
    assert(!(conflict.a.tick(conflict.now,true,true,false,true).faults & ConfigConflict));
    // Wrap-around arithmetic and arbitrary sensor failures cannot turn OFF.
    Controller wrap(1,1,commissioned(),true); wrap.sample(9000,true,UINT32_MAX-100);
    assert(!(wrap.tick(50,true,true,false,true).faults & Sensor));
    assert(wrap.tick(4000,true,true,false,true).faults & Sensor);
    std::mt19937 rng(42);
    for(int i=0;i<10000;++i) {
        Controller c(1,1,commissioned(),true); uint32_t now=rng();
        c.sample(int16_t(rng()),false,now); assert(c.tick(now,true,true,false,true).output);
    }
    auto packet=telemetry(p.fa,p.fb,true);
    if(argc==2) { FILE *file=fopen(argv[1],"wb"); assert(file); fwrite(&packet,1,sizeof(packet),file); fclose(file); }
    puts("PASS: control, hysteresis, minimum run, faults, freshness, one-way link, CRC, manual expiry, persistence, independent calibration, wrap, 10000 fail-on cases");
}
