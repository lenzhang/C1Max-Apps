#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace hidpilot {
struct Point { double x=0,y=0; };
using Quad=std::array<Point,4>; // top-left, top-right, bottom-right, bottom-left
inline bool valid_quad(const Quad&q){
    double area=0;
    for(int i=0;i<4;i++){
        auto a=q[i],b=q[(i+1)%4],c=q[(i+2)%4];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||a.x<0||a.x>1||a.y<0||a.y>1)return false;
        if((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x)<0.002)return false;
        area+=a.x*b.y-a.y*b.x;
    }return area>0.06;
}
class Homography {
    std::array<double,8> h_{};
public:
    explicit Homography(const Quad&q){
        if(!valid_quad(q))throw std::runtime_error("请按左上、右上、右下、左下选择完整屏幕，四角不可交叉");
        const Quad src={Point{0,0},{1,0},{1,1},{0,1}};double a[8][9]{};
        for(int i=0;i<4;i++){double x=src[i].x,y=src[i].y,u=q[i].x,v=q[i].y;int r=2*i;
            a[r][0]=x;a[r][1]=y;a[r][2]=1;a[r][6]=-u*x;a[r][7]=-u*y;a[r][8]=u;
            a[r+1][3]=x;a[r+1][4]=y;a[r+1][5]=1;a[r+1][6]=-v*x;a[r+1][7]=-v*y;a[r+1][8]=v;
        }
        for(int c=0;c<8;c++){int pivot=c;for(int r=c+1;r<8;r++)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
            if(std::abs(a[pivot][c])<1e-8)throw std::runtime_error("屏幕四角过于接近，请重新校准");
            for(int k=c;k<9;k++)std::swap(a[c][k],a[pivot][k]);double v=a[c][c];for(int k=c;k<9;k++)a[c][k]/=v;
            for(int r=0;r<8;r++)if(r!=c){double f=a[r][c];for(int k=c;k<9;k++)a[r][k]-=f*a[c][k];}
        }for(int i=0;i<8;i++)h_[i]=a[i][8];
    }
    Point map(double x,double y)const{double d=h_[6]*x+h_[7]*y+1;return {(h_[0]*x+h_[1]*y+h_[2])/d,(h_[3]*x+h_[4]*y+h_[5])/d};}
};
}
