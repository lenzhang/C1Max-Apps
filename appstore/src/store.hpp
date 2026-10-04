#pragma once
#include "net.hpp"
#include <atomic>
#include <functional>
namespace store {
struct App {std::string id,title,description,version,revision,url,sha256;uint64_t size=0,unpacked=0;};
struct Local {bool installed=false,visible=true;std::string version,revision,path,title;};
const std::vector<App>& builtin();
std::vector<App> fetch_catalog();
std::map<std::string,Local> load_state();
void set_visible(const std::string&id,bool visible);
void uninstall(const std::string&id);
void rollback();
void install(const App&,std::atomic<bool>&cancel,const std::function<void(unsigned,const std::string&)>&progress);
std::string compare(const Local&,const App&);
// Exposed for local package validation tests, never accepts external commands.
void unpack(const std::string&bundle,const std::string&destination,const App&);
std::string base();std::string home();
}
