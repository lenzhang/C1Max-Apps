// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>
namespace chat {
// The board advertises 4095 but shifts values left by 4 into the DW9714
// register without masking. Only the 10-bit position field (0..1023) is valid.
constexpr int dw9714_max_position=1023;
class FocusMotor {
    int fd_=-1,original_=0,position_=0,minimum_=0,maximum_=0,step_=1;
    bool changed_=false;
public:
    FocusMotor()=default;
    FocusMotor(const FocusMotor&)=delete;
    FocusMotor& operator=(const FocusMotor&)=delete;
    ~FocusMotor();
    bool open();
    bool move(int position);
    bool available()const{return fd_>=0;}
    int position()const{return position_;}
    int minimum()const{return minimum_;}
    int maximum()const{return maximum_;}
    int percent()const{return maximum_>minimum_?(position_-minimum_)*100/(maximum_-minimum_):0;}
};
// A bounded coarse/fine search, explicitly requested once per scan or via F.
// The worker controls settling time. No V4L2 or UI calls in this state machine.
class FocusSearch {
    std::vector<int> positions_;
    size_t index_=0;
    int low_=0,high_=0,origin_=0,best_=0,radius_=1;
    double best_score_=-1,origin_score_=0;
    bool fine_=false,active_=false,confident_=false;
public:
    void start(int low,int high,int origin);
    void cancel(){active_=false;}
    bool active()const{return active_;}
    int target()const{return positions_[index_];}
    int result()const{return confident_?best_:origin_;}
    bool confident()const{return confident_;}
    // Returns true if another position must be measured.
    bool sample(double score);
};
}
