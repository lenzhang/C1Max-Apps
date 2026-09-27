#pragma once
#include "agent.hpp"
namespace moonpilot {
std::string transcribe(const Settings&s,const std::string&wav);
std::string synthesize(const Settings&s,const std::string&text);
Json converse(const Settings&s,const std::string&utterance,const Json&history);
}
