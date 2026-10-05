#include "../src/voice.hpp"
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    assert(argc == 2);
    terminal::VoiceConfig config;
    config.enabled = true;
    config.endpoint = argv[1];
    config.model = "test-model";
    std::string wav(44, '\0');
    wav.replace(0, 4, "RIFF");
    wav.replace(8, 4, "WAVE");
    const auto text = terminal::request_voice(config, wav, "prompt> git status", 14, 80);
    assert(text == "git status --short");
    std::cout << "Terminal voice protocol passed\n";
}
