#pragma once
#include "net.hpp"
#include "usb_keys.hpp"
#include <string>
namespace moonpilot {
struct Settings {std::string endpoint,model,token;};
struct Action {std::string kind,summary,text;double x=0,y=0;int amount=0,ms=0;uint8_t key=0,mods=0,button=1;};
std::string base64(const std::vector<uint8_t>&data);
void validate_settings(const Settings&s);
Action parse_action(const Json&j);
Action decide(const Settings&s,const std::string&goal,const std::vector<uint8_t>&jpeg,const Json&history);
std::string describe(const Action&a);
}
