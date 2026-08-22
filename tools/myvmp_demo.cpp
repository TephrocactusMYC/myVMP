#include "myvmp/arm64/decoder.hpp"
#include "myvmp/bytecode/bytecode.hpp"
#include "myvmp/vm/vm.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    const std::vector<std::uint32_t> arm64_words = {
        0xD28000A0u, // movz x0, #5
        0x91001C00u, // add x0, x0, #7
        0xD1000800u, // sub x0, x0, #2
        0xD65F03C0u, // ret
    };

    const myvmp::arm64::Lifter lifter;
    auto ir = lifter.lift(arm64_words);
    const auto profile = myvmp::bytecode::profile_from_seed(0x20260812u);
    const auto image = myvmp::bytecode::encode(ir, {
        .key = 0x12345678u,
        .profile = profile,
        .diversify_immediates = true,
        .diversification_seed = profile.seed,
    });

    myvmp::vm::Interpreter vm;
    const auto result = vm.execute(image);
    std::cout << "protected_result=" << result.value << '\n';
    std::cout << "vm_instructions=" << result.instructions_executed << '\n';
    std::cout << profile.manifest() << '\n';
    return result.value == 10 ? 0 : 1;
}
