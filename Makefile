CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 -g
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
STRIP ?= strip
STATIC ?= 0
VERSION := $(strip $(shell cat VERSION))
CPPFLAGS += -DMYVMP_VERSION=\"$(VERSION)\"

BUILD_DIR := build
RELEASE_DIR := $(BUILD_DIR)/release/myvmp-$(VERSION)
RELEASE_BUILD_DIR := $(BUILD_DIR)/release-build
DIST_DIR := $(BUILD_DIR)/dist
HOST_ARCH ?= $(shell uname -m 2>/dev/null || echo unknown)
ifeq ($(STATIC),1)
LDFLAGS += -static
endif
LIB_OBJS := \
	$(BUILD_DIR)/src/arm32/decoder.o \
	$(BUILD_DIR)/src/arm64/decoder.o \
	$(BUILD_DIR)/src/bytecode/bytecode.o \
	$(BUILD_DIR)/src/elf/elf.o \
	$(BUILD_DIR)/src/elf/patch_plan.o \
	$(BUILD_DIR)/src/ir/ir.o \
	$(BUILD_DIR)/src/riscv64/decoder.o \
	$(BUILD_DIR)/src/patch/aarch64_trampoline.o \
	$(BUILD_DIR)/src/patch/arm32_trampoline.o \
	$(BUILD_DIR)/src/patch/riscv64_trampoline.o \
	$(BUILD_DIR)/src/patch/arm32_elf_writer.o \
	$(BUILD_DIR)/src/patch/elf_writer.o \
	$(BUILD_DIR)/src/protect/aarch64.o \
	$(BUILD_DIR)/src/protect/arm32.o \
	$(BUILD_DIR)/src/protect/riscv64.o \
	$(BUILD_DIR)/src/protect/sidecar.o \
	$(BUILD_DIR)/src/runtime/aarch64.o \
	$(BUILD_DIR)/src/runtime/arm32.o \
	$(BUILD_DIR)/src/runtime/profile_tables.o \
	$(BUILD_DIR)/src/runtime/riscv64.o \
	$(BUILD_DIR)/src/vm/multi_vm.o \
	$(BUILD_DIR)/src/vm/vm.o

.PHONY: all test version-check docs-check style-check check-release-tools release-check reproducible-check package-smoke-test error-contract-test generate-fixtures check-generated-fixtures generate-arm32-runtime check-arm32-runtime generate-aarch64-runtime check-aarch64-runtime generate-riscv64-runtime check-riscv64-runtime integration-test arm32-elf-parser-test arm32-vm-protect-test arm64-integration-test generated-fixture-test arm64-coverage-report arm64-liftability-report arm64-protect-dry-run-test arm64-elf-patch-test arm64-vm-protect-test arm64-simd-test arm64-far-elf-test arm64-micro-lift-test bytecode-seed-sweep multi-vm-quality-test riscv64-multi-vm-quality-test riscv64-elf-stress-test riscv64-fixture-test riscv64-liftability-report riscv64-vm-protect-test asm-diff release package clean

all: $(BUILD_DIR)/myvmp $(BUILD_DIR)/myvmp_demo $(BUILD_DIR)/myvmp_tests

$(BUILD_DIR)/myvmp: $(LIB_OBJS) $(BUILD_DIR)/tools/myvmp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/myvmp_demo: $(LIB_OBJS) $(BUILD_DIR)/tools/myvmp_demo.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/myvmp_tests: $(LIB_OBJS) $(BUILD_DIR)/tests/test_main.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(BUILD_DIR)/myvmp_tests
	$(BUILD_DIR)/myvmp_tests

version-check: $(BUILD_DIR)/myvmp
	@test "$$($(BUILD_DIR)/myvmp --version)" = "myVMP $(VERSION)"

docs-check:
	python3 scripts/check_docs.py

style-check:
	python3 scripts/check_style.py

check-release-tools:
	@command -v clang >/dev/null || { echo "release check requires clang (install the Ubuntu clang package)" >&2; exit 1; }
	@command -v readelf >/dev/null || { echo "release check requires readelf (install binutils)" >&2; exit 1; }

release-check: check-release-tools test version-check docs-check style-check error-contract-test check-aarch64-runtime check-riscv64-runtime check-arm32-runtime
	git diff --check

reproducible-check:
	python3 scripts/reproducible_release_check.py

package-smoke-test: package
	sh scripts/package_smoke_test.sh

error-contract-test: $(BUILD_DIR)/myvmp
	sh scripts/error_contract_test.sh

riscv64-fixture-test: $(BUILD_DIR)/myvmp
	bash scripts/riscv64_fixture_test.sh

riscv64-liftability-report: $(BUILD_DIR)/myvmp
	python3 scripts/riscv64_liftability_report.py

riscv64-vm-protect-test: all check-riscv64-runtime
	bash scripts/riscv64_vm_protect_test.sh

generate-fixtures:
	python3 scripts/generate_fixtures.py

check-generated-fixtures:
	sh scripts/check_generated_fixtures.sh

generate-aarch64-runtime:
	python3 scripts/generate_aarch64_runtime_blob.py

check-aarch64-runtime:
	python3 scripts/generate_aarch64_runtime_blob.py --check

generate-arm32-runtime:
	python3 scripts/generate_arm32_runtime_blob.py

check-arm32-runtime:
	python3 scripts/generate_arm32_runtime_blob.py --check

generate-riscv64-runtime:
	python3 scripts/generate_riscv64_runtime_blob.py

check-riscv64-runtime:
	python3 scripts/generate_riscv64_runtime_blob.py --check

integration-test: all
	sh scripts/integration_test.sh

arm32-elf-parser-test: all
	sh scripts/arm32_elf_parser_test.sh

arm32-vm-protect-test: all check-arm32-runtime
	sh scripts/arm32_vm_protect_test.sh

arm64-integration-test: all
	sh scripts/arm64_integration_test.sh

generated-fixture-test: all generate-fixtures
	sh scripts/generated_fixture_test.sh

arm64-coverage-report: generated-fixture-test
	python3 scripts/arm64_coverage_report.py

arm64-liftability-report: generated-fixture-test
	python3 scripts/arm64_liftability_report.py

arm64-protect-dry-run-test: generated-fixture-test
	sh scripts/arm64_protect_dry_run_test.sh

arm64-elf-patch-test: generated-fixture-test
	sh scripts/arm64_elf_patch_test.sh

arm64-vm-protect-test: generated-fixture-test
	sh scripts/arm64_integration_test.sh
	sh scripts/arm64_vm_protect_test.sh

arm64-simd-test: all check-aarch64-runtime
	sh scripts/aarch64_simd_test.sh

arm64-micro-lift-test: all
	sh scripts/arm64_micro_lift_test.sh

bytecode-seed-sweep: $(BUILD_DIR)/myvmp
	python3 scripts/bytecode_seed_sweep.py 16

multi-vm-quality-test: generated-fixture-test
	python3 scripts/multi_vm_quality.py 8

riscv64-multi-vm-quality-test: all check-riscv64-runtime
	python3 scripts/riscv64_multi_vm_quality.py 8

riscv64-elf-stress-test: all check-riscv64-runtime
	bash scripts/riscv64_elf_stress_test.sh

arm64-far-elf-test: all check-aarch64-runtime
	bash scripts/aarch64_far_elf_test.sh

asm-diff: integration-test
	sh scripts/compare_asm.sh

release:
	$(MAKE) BUILD_DIR=$(RELEASE_BUILD_DIR) CXX="$(CXX)" STATIC=$(STATIC) \
		CXXFLAGS='-std=c++20 -Wall -Wextra -Wpedantic -Werror -O3 -DNDEBUG' \
		$(RELEASE_BUILD_DIR)/myvmp
	mkdir -p $(RELEASE_DIR)
	cp $(RELEASE_BUILD_DIR)/myvmp $(RELEASE_DIR)/myvmp
	$(STRIP) $(RELEASE_DIR)/myvmp
	cp README.md $(RELEASE_DIR)/README.md
	cp README.zh-CN.md $(RELEASE_DIR)/README.zh-CN.md
	cp LICENSE NOTICE VERSION CHANGELOG.md CONTRIBUTING.md CONTRIBUTING.zh-CN.md \
		SECURITY.md SECURITY.zh-CN.md $(RELEASE_DIR)/
	mkdir -p $(RELEASE_DIR)/scripts
	cp scripts/generate_analysis_report.py $(RELEASE_DIR)/scripts/
	mkdir -p $(RELEASE_DIR)/docs
	find $(RELEASE_DIR)/docs -type f -name '*.md' -delete
	cp -R docs/. $(RELEASE_DIR)/docs/

package: release
	mkdir -p $(DIST_DIR)
	tar --sort=name --mtime=@$(if $(SOURCE_DATE_EPOCH),$(SOURCE_DATE_EPOCH),0) \
		--owner=0 --group=0 --numeric-owner -C $(dir $(RELEASE_DIR)) -czf \
		$(DIST_DIR)/myvmp-$(VERSION)-linux-$(HOST_ARCH)-tool.tar.gz $(notdir $(RELEASE_DIR))

clean:
	rm -rf $(BUILD_DIR)

-include $(LIB_OBJS:.o=.d)
-include $(BUILD_DIR)/tools/myvmp.d
-include $(BUILD_DIR)/tools/myvmp_demo.d
-include $(BUILD_DIR)/tests/test_main.d
