// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "single_layer_tests/classes/transpose.hpp"
#include "shared_test_classes/single_layer/transpose.hpp"
#include "test_utils/cpu_test_utils.hpp"
#include <cpp_interfaces/interface/ie_internal_plugin_config.hpp>

using namespace CPUTestUtils;
using namespace ov::test;

namespace CPULayerTestsDefinitions {
namespace Transpose {
namespace {
ov::AnyMap empty_config = {};
ov::AnyMap config_i64 = {{InferenceEngine::PluginConfigInternalParams::KEY_CPU_NATIVE_I64, InferenceEngine::PluginConfigParams::YES}};

const auto cpuParams_nchw = CPUSpecificParams {{nchw}, {}, {}, {}};

const auto cpuParams_ndhwc = CPUSpecificParams {{ndhwc}, {}, {}, {}};
const auto cpuParams_ncdhw = CPUSpecificParams {{ncdhw}, {}, {}, {}};

const auto cpuParams_nChw16c = CPUSpecificParams {{nChw16c}, {}, {}, {}};
const auto cpuParams_nCdhw16c = CPUSpecificParams {{nCdhw16c}, {}, {}, {}};

const auto cpuParams_nChw8c = CPUSpecificParams {{nChw8c}, {}, {}, {}};
const auto cpuParams_nCdhw8c = CPUSpecificParams {{nCdhw8c}, {}, {}, {}};

const std::vector<InputShape> staticInputShapes4DC32 = {InputShape{// dynamic
                                                                   {-1, 32, -1, -1},
                                                                   // target
                                                                   {{4, 32, 16, 14}, {16, 32, 5, 16}, {4, 32, 16, 14}}}};

const std::vector<ElementType> netPrecisions = {
        ElementType::i8,
        ElementType::bf16,
        ElementType::f32
};

const std::vector<CPUSpecificParams> CPUParams4D_blocked = {
        cpuParams_nChw16c,
        cpuParams_nChw8c,
};

const std::vector<CPUSpecificParams> CPUParams4D = {
        cpuParams_nChw16c,
        cpuParams_nChw8c,
        cpuParams_nchw,
};

const std::vector<InputShape> staticInputShapes4DC16 = {InputShape{// dynamic
                                                                   {-1, 16, -1, -1},
                                                                   // target
                                                                   {{2, 16, 21, 10}, {3, 16, 11, 12}, {2, 16, 21, 10}}}};

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes4DC16_TransposeBlocked, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes4DC16()),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::ValuesIn(netPrecisions),
                                 ::testing::Values(empty_config),
                                 ::testing::ValuesIn(CPUParams4D_blocked)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes4DC32_TransposeBlocked, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes4DC32()),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::ValuesIn(netPrecisions),
                                 ::testing::Values(empty_config),
                                 ::testing::ValuesIn(CPUParams4D_blocked)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_dynamicShapes4D_Transpose, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes4D()),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::Values(ElementType::bf16),
                                 ::testing::Values(empty_config),
                                 ::testing::Values(CPUSpecificParams{})),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes4DC16_Transpose_I64, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes4DC16),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::Values(ElementType::i64),
                                 ::testing::Values(config_i64),
                                 ::testing::ValuesIn(CPUParams4D)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes4DC32_Transpose_i64, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes4DC32),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::Values(ElementType::i64),
                                 ::testing::Values(config_i64),
                                 ::testing::ValuesIn(CPUParams4D)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_dynamicShapes4D_Transpose_I64, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes4D()),
                                 ::testing::ValuesIn(inputOrder4D()),
                                 ::testing::Values(ElementType::i64),
                                 ::testing::Values(config_i64),
                                 ::testing::Values(CPUSpecificParams{})),
                         TransposeLayerCPUTest::getTestCaseName);

const std::vector<InputShape> staticInputShapes5DC16 = {InputShape{
    // dynamic
    {-1, 16, -1, -1, -1},
    // Static shapes
    {{2, 16, 5, 6, 5}, {3, 16, 6, 5, 6}, {2, 16, 5, 6, 5}}}
};

const std::vector<InputShape> staticInputShapes5DC32 = {InputShape{
    // dynamic
    {-1, 32, -1, -1, -1},
    // Static shapes
    {{4, 32, 5, 6, 5}, {5, 32, 6, 5, 6}, {4, 32, 5, 6, 5}}}
};

const std::vector<InputShape> dynamicInputShapes5D = {InputShape{
    // dynamic
    {ov::Dimension(1, 20), ov::Dimension(5, 150), ov::Dimension(5, 40), ov::Dimension(5, 40), ov::Dimension(5, 40)},
    // target
    {{1, 32, 5, 6, 5}, {2, 32, 6, 5, 6}, {4, 55, 5, 6, 5}, {3, 129, 6, 5, 6}, {1, 32, 5, 6, 5}}}
};

const std::vector<std::vector<size_t>> inputOrder5D = {
        std::vector<size_t>{0, 1, 2, 3, 4},
        std::vector<size_t>{0, 4, 2, 3, 1},
        std::vector<size_t>{0, 4, 2, 1, 3},
        std::vector<size_t>{0, 2, 3, 4, 1},
        std::vector<size_t>{0, 2, 4, 3, 1},
        std::vector<size_t>{0, 3, 2, 4, 1},
        std::vector<size_t>{0, 3, 1, 4, 2},
        std::vector<size_t>{1, 0, 2, 3, 4},
        std::vector<size_t>{},
};

const std::vector<std::vector<size_t>> inputOrderPerChannels5D = {
        std::vector<size_t>{0, 1, 2, 3, 4},
        std::vector<size_t>{0, 4, 2, 3, 1},
        std::vector<size_t>{0, 4, 2, 1, 3},
        std::vector<size_t>{0, 2, 4, 3, 1},
        std::vector<size_t>{0, 3, 2, 4, 1},
        std::vector<size_t>{0, 3, 1, 4, 2},
        std::vector<size_t>{1, 0, 2, 3, 4},
        std::vector<size_t>{},
};

const std::vector<CPUSpecificParams> CPUParams5D = {
        cpuParams_nCdhw16c,
        cpuParams_nCdhw8c,
        cpuParams_ncdhw,
};

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes5DC16_Transpose, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes5DC16),
                                 ::testing::ValuesIn(inputOrder5D),
                                 ::testing::ValuesIn(netPrecisions),
                                 ::testing::Values(empty_config),
                                 ::testing::ValuesIn(CPUParams5D)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes5DC32_Transpose, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes5DC32),
                                 ::testing::ValuesIn(inputOrder5D),
                                 ::testing::ValuesIn(netPrecisions),
                                 ::testing::Values(empty_config),
                                 ::testing::ValuesIn(CPUParams5D)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_dynamicShapes5D_Transpose, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes5D),
                                 ::testing::ValuesIn(inputOrder5D),
                                 ::testing::Values(ElementType::bf16),
                                 ::testing::Values(empty_config),
                                 ::testing::Values(CPUSpecificParams{})),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes5DC16_PermutePerChannels, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes5DC16),
                                 ::testing::ValuesIn(inputOrderPerChannels5D),
                                 ::testing::ValuesIn(netPrecisionsPerChannels()),
                                 ::testing::Values(empty_config),
                                 ::testing::Values(cpuParams_ndhwc)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_staticShapes5DC32_PermutePerChannels, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(staticInputShapes5DC32),
                                 ::testing::ValuesIn(inputOrderPerChannels5D),
                                 ::testing::ValuesIn(netPrecisionsPerChannels()),
                                 ::testing::Values(empty_config),
                                 ::testing::Values(cpuParams_ndhwc)),
                         TransposeLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_dynamicShapes5D_PermutePerChannels, TransposeLayerCPUTest,
                         ::testing::Combine(
                                 ::testing::ValuesIn(dynamicInputShapes5D),
                                 ::testing::ValuesIn(inputOrderPerChannels5D),
                                 ::testing::ValuesIn(netPrecisionsPerChannels()),
                                 ::testing::Values(empty_config),
                                 ::testing::Values(CPUSpecificParams{})),
                         TransposeLayerCPUTest::getTestCaseName);
} // namespace
} // namespace Transpose
} // namespace CPULayerTestsDefinitions
