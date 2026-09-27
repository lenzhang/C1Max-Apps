#include "../src/reports.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace hidpilot;
int main(){
    // Decode the HID descriptor independently and compare its wire report sizes.
    std::map<int,int> input,output; int size=0,count=0,id=0;
    const auto&d=report_descriptor();
    for(size_t i=0;i<d.size();) {
        int tag=d[i++],n=tag&3;if(n==3)n=4;assert(i+n<=d.size());
        unsigned value=0;for(int j=0;j<n;j++)value|=unsigned(d[i++])<<(8*j);
        switch(tag&0xfc) {case 0x74:size=value;break;case 0x94:count=value;break;case 0x84:id=value;break;case 0x80:input[id]+=size*count;break;case 0x90:output[id]+=size*count;break;}
    }
    assert(input[1]==64&&input[2]==48&&input[3]==32&&output[1]==8);
    assert(descriptors().size()==70);
    for(auto report:{keyboard(4,2),keyboard(),absolute(-1,40000),relative(500,-500)})assert(valid_report(report.data(),report.size()));
    auto abs=absolute(-1,40000);assert(abs[2]==0&&abs[3]==0&&abs[4]==255&&abs[5]==127);
    auto bad=keyboard();bad[2]=1;assert(!valid_report(bad.data(),bad.size()));bad=keyboard(102);assert(!valid_report(bad.data(),bad.size()));
    bad=absolute(0,0);bad[3]=128;assert(!valid_report(bad.data(),bad.size()));assert(!valid_report(nullptr,0));
    for(unsigned c=32;c<127;c++) {auto k=ascii(c);assert(k[0]&&k[0]<=0x65);}
    assert(ascii('A')[0]==4&&ascii('A')[1]==2&&ascii('z')[0]==29);
    assert(ascii('/')[0]==56&&ascii('?')[0]==56&&ascii('?')[1]==2);
    assert(ascii('\n')[0]==40&&ascii('\b')[0]==42&&ascii(0xE4)[0]==0);
std::cout<<"HID report layout and key map passed\n";
}
