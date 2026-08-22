#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

typedef uint64_t (*candidate_fn)(uint64_t);

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    candidate_fn candidate = (candidate_fn)dlsym(handle, "candidate_global_ops");
    if (candidate == NULL) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return 1;
    }
    const uint64_t result = candidate(0x55aau);
    printf("shared_candidate_global_ops=%llu\n", (unsigned long long)result);
    dlclose(handle);
    return result == ((0x12345678ull ^ 0x55aaull) + 17ull) ? 0 : 1;
}
