// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <ngraph_functions/builders.hpp>
#include <common_test_utils/ov_tensor_utils.hpp>
#include "test_utils/cpu_test_utils.hpp"
#include "shared_test_classes/base/ov_subgraph.hpp"
#include <cpp_interfaces/interface/ie_internal_plugin_config.hpp>

using namespace InferenceEngine;
using namespace CPUTestUtils;
using namespace ov::test;

namespace CPULayerTestsDefinitions {

using oneHotCPUTestParams = std::tuple<
        InputShape,                                        // Input shape
        int,                                               // axis to extend
        std::pair<ngraph::helpers::InputLayerType, bool>,  // secondary input type && need to generate depth
        size_t,                                            // depth
        float,                                             // on_value
        float,                                             // off_value
        ElementType,                                       // Input precision
        ElementType,                                       // Output precision
        ov::AnyMap,                                        // Additional network configuration
        CPUSpecificParams>;

class OneHotLayerCPUTest : public testing::WithParamInterface<oneHotCPUTestParams>,
                           virtual public SubgraphBaseTest, public CPUTestsBase {
public:
    static std::string getTestCaseName(const testing::TestParamInfo<oneHotCPUTestParams>& obj) {
        InputShape inputShape;
        int axis;
        std::pair<ngraph::helpers::InputLayerType, bool> inputType;
        size_t depth;
        float onValue, offValue;
        ElementType inPrc, outPrc;
        ov::AnyMap additionalConfig;
        CPUSpecificParams cpuParams;
        std::tie(inputShape, axis, inputType, depth, onValue, offValue, inPrc, outPrc, additionalConfig, cpuParams) = obj.param;

        std::ostringstream result;
        if (inputShape.first.size() != 0) {
            result << "IS=(" << CommonTestUtils::partialShape2str({inputShape.first}) << "_";
        }
        result << "TS=";
        for (const auto& shape : inputShape.second) {
                result << CommonTestUtils::vec2str(shape) << "_";
        }
        result << "axis=" << axis << "_";
        if (inputType.first == ngraph::helpers::InputLayerType::CONSTANT && !inputType.second) {
            result << "depth=" << depth << "_";
        } else if (inputType.first == ngraph::helpers::InputLayerType::CONSTANT && inputType.second) {
            result << "depth=WillBeGenerated" << "_";
        } else {
            result << "depth=PARAMETER" << "_";
        }
        result << "OnVal=" << onValue << "_";
        result << "OffVal=" << offValue << "_";
        result << "inPRC=" << inPrc << "_";
        result << "outPRC=" << outPrc;
        if (!additionalConfig.empty()) {
            result << "_PluginConf";
            for (auto &item : additionalConfig) {
                result << "_" << item.first << "=";
                item.second.print(result);
            }
        }
        result << CPUTestsBase::getTestCaseName(cpuParams);

        return result.str();
    }

    void generate_inputs(const std::vector<ov::Shape>& targetInputStaticShapes) override {
        inputs.clear();
        const auto& funcInputs = function->inputs();
        for (size_t i = 0; i < funcInputs.size(); ++i) {
            const auto& funcInput = funcInputs[i];
            ov::Tensor tensor;

            if (i == 1) {
                tensor = ov::Tensor(funcInput.get_element_type(), targetInputStaticShapes[i]);
                if (funcInput.get_element_type() == ElementType::i64) {
                    tensor.data<int64_t>()[0] = Depth;
                } else {
                    tensor.data<int32_t>()[0] = Depth;
                }
            } else {
                tensor = utils::create_and_fill_tensor(funcInput.get_element_type(), targetInputStaticShapes[i]);
            }

            inputs.insert({funcInput.get_node_shared_ptr(), tensor});
        }
    }
protected:
    void SetUp() override {
        targetDevice = CommonTestUtils::DEVICE_CPU;

        InputShape inputShape;
        std::pair<ngraph::helpers::InputLayerType, bool> inputType;
        CPUSpecificParams cpuParams;
        std::tie(inputShape, Axis, inputType, Depth, OnValue, OffValue, inType, outType, configuration, cpuParams) = this->GetParam();

        if (inputType.second && inputType.first == ngraph::helpers::InputLayerType::CONSTANT) {
            generateDepth();
        }

        std::tie(inFmts, outFmts, priority, selectedType) = cpuParams;
        if (inType == ElementType::i64 || inType == ElementType::u64) {
            auto i64Flag = configuration.find(PluginConfigInternalParams::KEY_CPU_NATIVE_I64);
            if (i64Flag == configuration.end() || i64Flag->second == PluginConfigParams::NO) {
                selectedType = makeSelectedTypeStr(selectedType, ElementType::i32);
            } else {
                selectedType = makeSelectedTypeStr(selectedType, ElementType::i64);
            }
        } else {
            selectedType = makeSelectedTypeStr(selectedType, inType);
        }

        init_input_shapes({inputShape});
        if (inputType.second) {
            for (auto &target : targetStaticShapes)
                target.push_back({});
        }

        function = createFunction(inputType.first == ngraph::helpers::InputLayerType::CONSTANT);
    }

    void init_ref_function(std::shared_ptr<ov::Model> &funcRef, const std::vector<ov::Shape>& targetInputStaticShapes) override {
        if (function->get_parameters().size() == 2) {
            generateDepth();
            funcRef = createFunction(true);
        }
        ngraph::helpers::resize_function(funcRef, targetInputStaticShapes);
    }

    void validate() override {
            auto actualOutputs = get_plugin_outputs();
        if (function->get_parameters().size() == 2) {
            auto pos = std::find_if(inputs.begin(), inputs.end(),
                                    [](const std::pair<std::shared_ptr<ov::Node>, ov::Tensor> &params) {
                                        return params.first->get_friendly_name() == "ParamDepth";
                                    });
            IE_ASSERT(pos != inputs.end());
            inputs.erase(pos);
        }
        auto expectedOutputs = calculate_refs();
        if (expectedOutputs.empty()) {
                return;
        }
        ASSERT_EQ(actualOutputs.size(), expectedOutputs.size())
                << "nGraph interpreter has " << expectedOutputs.size() << " outputs, while IE " << actualOutputs.size();

        compare(expectedOutputs, actualOutputs);
    }

    std::shared_ptr<ov::Model> createFunction(bool depthConst) {
        auto params = ngraph::builder::makeDynamicParams(inType, {inputDynamicShapes.front()});
        params.front()->set_friendly_name("ParamsIndices");
        std::shared_ptr<ov::Node> depth;
        if (depthConst) {
            depth = ov::op::v0::Constant::create(inType, ov::Shape{ }, {Depth});
        } else {
            auto depthParam = std::make_shared<ov::op::v0::Parameter>(inType, ov::Shape{ });
            depthParam->set_friendly_name("ParamDepth");
            params.push_back(depthParam);
            depth = depthParam;
        }
        auto paramOuts = ngraph::helpers::convert2OutputVector(ngraph::helpers::castOps2Nodes<ov::op::v0::Parameter>(params));
        auto on_value_const = std::make_shared<ov::op::v0::Constant>(outType, ov::Shape{ }, OnValue);
        auto off_value_const = std::make_shared<ov::op::v0::Constant>(outType, ov::Shape{ }, OffValue);
        auto oneHot = std::make_shared<ov::op::v1::OneHot>(paramOuts[0], depth, on_value_const, off_value_const, Axis);
        return makeNgraphFunction(inType, params, oneHot, "OneHot");
    }

    void generateDepth() {
        testing::internal::Random random(time(nullptr));
        random.Generate(10);
        Depth = static_cast<int64_t>(1 + static_cast<int64_t>(random.Generate(10)));
    }

    int Axis;
    size_t Depth;
    float OnValue, OffValue;
};

TEST_P(OneHotLayerCPUTest, CompareWithRefs) {
    run();
    CheckPluginRelatedResults(compiledModel, "OneHot");
}

namespace {
const std::vector<ElementType> inPrc = {
        ElementType::i32,
};

const std::vector<ElementType> outPrc = {
        ElementType::f32,
        ElementType::bf16,
        ElementType::i8
        // ElementType::u8  // Precision cannot be wrapped to constant one hot
};

const CPUSpecificParams cpuParamsRef{{}, {}, {"ref_any"}, "ref_any"};

std::vector<std::pair<ngraph::helpers::InputLayerType, bool>> secondaryInputTypesStaticCase = {
        {ngraph::helpers::InputLayerType::CONSTANT, true},
        {ngraph::helpers::InputLayerType::CONSTANT, false}
};
std::vector<std::pair<ngraph::helpers::InputLayerType, bool>> secondaryInputTypesDynamicCase = {
        {ngraph::helpers::InputLayerType::CONSTANT, true},
        {ngraph::helpers::InputLayerType::CONSTANT, false},
        {ngraph::helpers::InputLayerType::PARAMETER, true}
};

const std::vector<ov::Shape> staticInputShapes0D = {
        { }
};

const ov::AnyMap i64Config = {
        {PluginConfigInternalParams::KEY_CPU_NATIVE_I64, PluginConfigParams::YES}
};
const ov::AnyMap emptyConfig = {};

// 0d -> 1d, depth
const auto testCase_1d = ::testing::Combine(
        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes0D)),
        ::testing::Values(-1, 0),
        ::testing::ValuesIn(secondaryInputTypesStaticCase),
        ::testing::Values(3),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_1D, OneHotLayerCPUTest, testCase_1d, OneHotLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_1D_I64, OneHotLayerCPUTest,
                ::testing::Combine(
                        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes0D)),
                        ::testing::Values(-1, 0),
                        ::testing::ValuesIn(secondaryInputTypesStaticCase),
                        ::testing::Values(3),
                        ::testing::Values(1.f),
                        ::testing::Values(0.f),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(i64Config),
                        ::testing::Values(cpuParamsRef)),
                OneHotLayerCPUTest::getTestCaseName);

const std::vector<ov::Shape> staticInputShapes1D = {
        { 3 }
};
// 1d -> 2d, axis default
const auto testCase_2d_static = ::testing::Combine(
        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes1D)),
        ::testing::Values(-1, 0, 1),
        ::testing::ValuesIn(secondaryInputTypesStaticCase),
        ::testing::Values(6),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_2D_Static, OneHotLayerCPUTest, testCase_2d_static, OneHotLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_2D_I64_Static, OneHotLayerCPUTest,
                ::testing::Combine(
                        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes1D)),
                        ::testing::Values(-1, 0, 1),
                        ::testing::ValuesIn(secondaryInputTypesStaticCase),
                        ::testing::Values(6),
                        ::testing::Values(1.f),
                        ::testing::Values(0.f),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(i64Config),
                        ::testing::Values(cpuParamsRef)),
                OneHotLayerCPUTest::getTestCaseName);

const std::vector<InputShape> dynamicInputShapes1D = {
        {{-1}, {{3}, {4}, {5}}},
        {{{1, 5}}, {{1}, {3}, {5}}},
};
// 1d -> 2d, axis default
const auto testCase_2d_dynamic = ::testing::Combine(
        ::testing::ValuesIn(dynamicInputShapes1D),
        ::testing::Values(-1, 0, 1),
        ::testing::ValuesIn(secondaryInputTypesDynamicCase),
        ::testing::Values(6),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_2D_Dynamic, OneHotLayerCPUTest, testCase_2d_dynamic, OneHotLayerCPUTest::getTestCaseName);

const std::vector<ov::Shape> staticInputShapes2D = {
        { 3, 2 }
};
// 2d -> 3d, on_value, off_value
const auto testCase_3d_static = ::testing::Combine(
        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes2D)),
        ::testing::Values(-1, 0, 1),
        ::testing::ValuesIn(secondaryInputTypesStaticCase),
        ::testing::Values(4),
        ::testing::Values(2.f),
        ::testing::Values(-1.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_3D_Static, OneHotLayerCPUTest, testCase_3d_static, OneHotLayerCPUTest::getTestCaseName);

const std::vector<InputShape> dynamicInputShapes2D = {
        {{-1, -1}, {{3, 2}, {2, 3}, {4, 4}}},
        {{-1, 3}, {{2, 3}, {3, 3}, {4, 3}}},
        {{{1, 5}, {3, 4}}, {{2, 3}, {3, 4}, {4, 3}}}
};
// 2d -> 3d, on_value, off_value
const auto testCase_3d_dynamic = ::testing::Combine(
        ::testing::ValuesIn(dynamicInputShapes2D),
        ::testing::Values(-1, 0, 1),
        ::testing::ValuesIn(secondaryInputTypesDynamicCase),
        ::testing::Values(4),
        ::testing::Values(2.f),
        ::testing::Values(-1.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_3D_Dynamic, OneHotLayerCPUTest, testCase_3d_dynamic, OneHotLayerCPUTest::getTestCaseName);

const std::vector<ov::Shape> staticInputShapes3D = {
        { 1, 3, 2 }
};
// 3d -> 4d
const auto testCase_4d_static = ::testing::Combine(
        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes3D)),
        ::testing::Values(-1, 0, 1, 2),
        ::testing::ValuesIn(secondaryInputTypesStaticCase),
        ::testing::Values(4),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_4D_Static, OneHotLayerCPUTest, testCase_4d_static, OneHotLayerCPUTest::getTestCaseName);

const std::vector<InputShape> dynamicInputShapes3D = {
        {{-1, -1, -1}, {{1, 3, 2}, {1, 2, 3}, {2, 4, 4}}},
        {{-1, 3, -1}, {{2, 3, 1}, {1, 3, 2}, {1, 3, 5}}},
        {{{1, 2}, 3, {1, 5}}, {{2, 3, 1}, {1, 3, 2}, {1, 3, 5}}}
};
// 3d -> 4d
const auto testCase_4d_dynamic = ::testing::Combine(
        ::testing::ValuesIn(dynamicInputShapes3D),
        ::testing::Values(-1, 0, 1, 2),
        ::testing::ValuesIn(secondaryInputTypesDynamicCase),
        ::testing::Values(4),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_4D_Dynamic, OneHotLayerCPUTest, testCase_4d_dynamic, OneHotLayerCPUTest::getTestCaseName);

INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_4D_I64_Dynamic, OneHotLayerCPUTest,
                ::testing::Combine(
                        ::testing::ValuesIn(dynamicInputShapes3D),
                        ::testing::Values(-1, 0, 1, 2),
                        ::testing::ValuesIn(secondaryInputTypesDynamicCase),
                        ::testing::Values(4),
                        ::testing::Values(1.f),
                        ::testing::Values(0.f),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(ElementType::i64),
                        ::testing::Values(i64Config),
                        ::testing::Values(cpuParamsRef)),
                OneHotLayerCPUTest::getTestCaseName);

const std::vector<ov::Shape> staticInputShapes4D = {
        { 1, 3, 2, 3 }
};
// 4d -> 5d
const auto testCase_5d_static = ::testing::Combine(
        ::testing::ValuesIn(static_shapes_to_test_representation(staticInputShapes4D)),
        ::testing::Values(-1, 0, 1, 2, 3),
        ::testing::ValuesIn(secondaryInputTypesStaticCase),
        ::testing::Values(4),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_5D_Static, OneHotLayerCPUTest, testCase_5d_static, OneHotLayerCPUTest::getTestCaseName);

const std::vector<InputShape> dynamicInputShapes4D = {
        {{-1, -1, -1, -1}, {{1, 3, 2, 3}, {1, 2, 3, 2}, {2, 3, 4, 4}}},
        {{-1, 3, -1, {1, 3}}, {{1, 3, 3, 1}, {1, 3, 2, 2}, {1, 3, 5, 3}}},
        {{{1, 2}, 3, {2, 5}, {1, 3}}, {{1, 3, 3, 1}, {2, 3, 2, 2}, {1, 3, 5, 3}}}
};
// 4d -> 5d
const auto testCase_5d_dynamic = ::testing::Combine(
        ::testing::ValuesIn(dynamicInputShapes4D),
        ::testing::Values(-1, 0, 1, 2, 3),
        ::testing::ValuesIn(secondaryInputTypesDynamicCase),
        ::testing::Values(4),
        ::testing::Values(1.f),
        ::testing::Values(0.f),
        ::testing::ValuesIn(inPrc),
        ::testing::ValuesIn(outPrc),
        ::testing::Values(emptyConfig),
        ::testing::Values(cpuParamsRef)
);
INSTANTIATE_TEST_SUITE_P(smoke_OneHotCPU_5D_Dynamic, OneHotLayerCPUTest, testCase_5d_dynamic, OneHotLayerCPUTest::getTestCaseName);

} // namespace
} // namespace CPULayerTestsDefinitions
