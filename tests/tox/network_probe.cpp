#include "engine.hpp"
#include <chrono>
#include <iostream>
int main(int argc,char**argv){if(argc!=3)return 2;chat::Engine e(argv[1],argv[2]);for(int i=0;i<1200;i++){auto s=e.snapshot();if(!s.error.empty()){std::cerr<<s.error<<'\n';return 1;}if(s.online){std::cout<<s.status<<'\n';return 0;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}std::cerr<<"No Tox network connection within 60 seconds\n";return 1;}
