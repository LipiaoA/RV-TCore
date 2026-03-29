#ifndef RV_TCORE_DIV_H
#define RV_TCORE_DIV_H



// Integer DIV Unit: n-cycle 逻辑延迟，默认按 10-cycle 建模，可通过 set_latency 覆盖。
class DivUnit : public DevIntf {
public:
    DivUnit() : DevIntf("div") {
    }
    void build() {}
    void dispatch(const MicroOp& inst) {
        return;
    }
    bool accept() const {
        return false;
    }
    void tick() {
        return;
    }
};
