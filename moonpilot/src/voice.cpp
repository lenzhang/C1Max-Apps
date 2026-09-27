#include "voice.hpp"
#include <stdexcept>
namespace moonpilot {
std::string transcribe(const Settings&s,const std::string&wav){
    validate_settings(s);if(wav.size()<44||wav.size()>700000||wav.compare(0,4,"RIFF")||wav.compare(8,4,"WAVE"))throw std::runtime_error("录音无效或过长");
    const std::string boundary="----C1MoonPilotAudio26";
    if(s.model.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("识别模型名无效");
    std::string body="--"+boundary+"\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n"+s.model+"\r\n--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n"+wav+"\r\n--"+boundary+"--\r\n";
    std::vector<std::string> headers={"Content-Type: multipart/form-data; boundary="+boundary};if(!s.token.empty())headers.push_back("Authorization: Bearer "+s.token);
    auto r=c1::http("POST",s.endpoint,headers,body,nullptr,90);if(r.status<200||r.status>=300)throw std::runtime_error("语音识别 HTTP "+std::to_string(r.status));
    auto text=Json::parse(r.body).at("text").get<std::string>();if(text.empty()||text.size()>1200)throw std::runtime_error("没有识别到有效语音");return text;
}
std::string synthesize(const Settings&s,const std::string&text){
    validate_settings(s);if(text.empty()||text.size()>600)throw std::runtime_error("朗读内容过长");
    Json body={{"model",s.model},{"input",text},{"response_format","wav"}}; // Use server's configured voice, not a file named "none".
    std::vector<std::string> headers={"Content-Type: application/json"};if(!s.token.empty())headers.push_back("Authorization: Bearer "+s.token);
    auto r=c1::http("POST",s.endpoint,headers,body.dump(),nullptr,90);if(r.status<200||r.status>=300)throw std::runtime_error("语音合成 HTTP "+std::to_string(r.status));
    if(r.body.size()<44||r.body.compare(0,4,"RIFF")||r.body.compare(8,4,"WAVE"))throw std::runtime_error("语音服务未返回 WAV 音频");return r.body;
}
Json converse(const Settings&s,const std::string&utterance,const Json&history){
    validate_settings(s);if(utterance.empty()||utterance.size()>1200)throw std::runtime_error("语音内容无效");
    Json messages=Json::array({{{"role","system"},{"content","你是 MoonPilot，一个通过 Moonlight 查看和操作远程电脑的助手。用简短自然中文回答，每次最多 100 字。当前这一调用没有图像、没有执行电脑动作，不能假装看见屏幕或完成任务。用户聊天/提问时只回答；用户明确让你操作电脑时，把目标放在 task，说明已准备好，按运行后开始。只返回 JSON：{\"reply\":\"简短回答\",\"task\":null}，或 task 为清晰的任务字符串。"}}});
    for(auto&m:history)messages.push_back(m);messages.push_back({{"role","user"},{"content",utterance}});
    Json body={{"model",s.model},{"messages",messages},{"max_tokens",512},{"temperature",0.3},{"stream",false},{"response_format",{{"type","json_object"}}},{"chat_template_kwargs",{{"enable_thinking",false}}}};
    std::vector<std::string> headers={"Content-Type: application/json"};if(!s.token.empty())headers.push_back("Authorization: Bearer "+s.token);
    auto r=c1::http("POST",s.endpoint,headers,body.dump(),nullptr,90);if(r.status<200||r.status>=300)throw std::runtime_error("对话接口 HTTP "+std::to_string(r.status));
    auto m=Json::parse(r.body).at("choices").at(0).at("message");auto text=m.value("content",std::string());if(text.empty())text=m.value("reasoning",std::string());
    Json out;try{out=Json::parse(text);}catch(...){throw std::runtime_error("对话模型未返回有效 JSON");}
    auto reply=out.at("reply").get<std::string>();if(reply.empty()||reply.size()>600)throw std::runtime_error("对话回答无效或过长");
    if(out.contains("task")&&!out["task"].is_null()){auto task=out["task"].get<std::string>();if(task.empty()||task.size()>1200)throw std::runtime_error("对话任务格式错误");}
    return out;
}
}
