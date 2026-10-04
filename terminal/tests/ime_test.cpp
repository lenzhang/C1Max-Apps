#include "c1ime.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
    assert(argc == 3);
    const std::filesystem::path user(argv[2]);
    {
        const auto missing = user / "empty-shared";
        std::filesystem::create_directories(missing);
        c1ime::Engine engine(missing.string(), (user / "missing-user").string());
        assert(!engine.initialize());
        assert(!engine.ready());
        assert(engine.error().find("prebuilt") != std::string::npos);
        assert(!std::filesystem::exists(user / "missing-user/build/luna_pinyin.table.bin"));
    }
    {
        c1ime::Engine engine(argv[1], (user / "valid-user").string());
        assert(engine.initialize());
        assert(engine.mode() == c1ime::Mode::English);
        assert(!engine.input('n'));
        engine.toggle_mode();
        for (char ch : std::string("nihao")) assert(engine.input(ch));
        const auto candidates = engine.candidates();
        assert(!candidates.empty());
        assert(candidates[0].text == "你好");
        assert(engine.select(0) == "你好");
        assert(engine.state() == c1ime::State::Inactive);
        assert(engine.input('n'));
        assert(engine.input('i'));
        engine.page_down();
        engine.page_up();
        assert(engine.backspace());
        engine.cancel();
        assert(engine.state() == c1ime::State::Inactive);
        engine.toggle_mode();
        assert(engine.mode() == c1ime::Mode::English);
    }
    std::cout << "Rime: prebuilt data, Chinese candidates/commit, editing and missing-data fallback PASS\n";
}
