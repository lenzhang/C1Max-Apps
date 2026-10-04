// Build-host helper, executed through QEMU so Rime's mapped data uses the
// target ABI. Never packaged or run on the dictionary.
#include <rime_api.h>
#include <cstring>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 5 || std::strcmp(argv[1], "--build")) return 2;
    auto *api = rime_get_api();
    RIME_STRUCT(RimeTraits, traits);
    traits.user_data_dir = argv[2];
    traits.shared_data_dir = argv[3];
    traits.staging_dir = argv[4];
    traits.app_name = "rime.c1max.build";
    traits.distribution_code_name = "c1max";
    traits.distribution_version = "1";
    traits.min_log_level = 2;
    api->setup(&traits);
    api->deployer_initialize(&traits);
    const bool ok = api->deploy();
    api->finalize();
    if (!ok) std::cerr << "Rime dictionary deployment failed\n";
    return ok ? 0 : 1;
}
