// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "reduce.hpp"

#include <common_test_utils/ov_tensor_utils.hpp>
#include <cpp_interfaces/interface/ie_internal_plugin_config.hpp>

using namespace InferenceEngine;
using namespace CPUTestUtils;
using namespace ov::test;

namespace CPULayerTestsDefinitions {

std::string ReduceCPULayerTest::getTestCaseName(testing::TestParamInfo<ReduceLayerCPUTestParamSet> obj) {
    basicReduceParams basicParams;
    CPUSpecificParams cpuParams;
    fusingSpecificParams fusingParams;
    std::tie(basicParams, cpuParams, fusingParams) = obj.param;

    std::vector<int> axes;
    CommonTestUtils::OpType opType;
    bool keepDims;
    ngraph::helpers::ReductionType reductionType;
    ElementType netPrecision, inPrc, outPrc;
    std::vector<InputShape> inputShapes;
    ov::AnyMap config;

    std::tie(axes, opType, keepDims, reductionType, netPrecision, inPrc, outPrc, inputShapes, config) = basicParams;

    std::ostringstream result;
    result << "IS=(";
    for (const auto& shape : inputShapes) {
        result << CommonTestUtils::partialShape2str({shape.first}) << "_";
    }
    result << ")_TS=(";
    for (const auto& shape : inputShapes) {
        for (const auto& item : shape.second) {
            result << CommonTestUtils::vec2str(item) << "_";
        }
    }
    result << ")_axes=" << CommonTestUtils::vec2str(axes) << "_";
    result << "opType=" << opType << "_";
    result << "type=" << reductionType << "_";
    if (keepDims)
        result << "KeepDims=true_";
    else
        result << "KeepDims=false_";
    result << "netPRC=" << netPrecision << "_";
    result << "inPRC=" << inPrc << "_";
    result << "outPRC=" << outPrc;

    if (!config.empty()) {
        result << "_PluginConf";
        for (const auto& configItem : config) {
            result << "_" << configItem.first << "=";
            configItem.second.print(result);
        }
    }

    result << CPUTestsBase::getTestCaseName(cpuParams);
    result << CpuTestWithFusing::getTestCaseName(fusingParams);

    return result.str();
}

void ReduceCPULayerTest::SetUp() {
    targetDevice = CommonTestUtils::DEVICE_CPU;

    basicReduceParams basicParams;
    CPUSpecificParams cpuParams;
    fusingSpecificParams fusingParams;
    std::tie(basicParams, cpuParams, fusingParams) = this->GetParam();

    std::tie(inFmts, outFmts, priority, selectedType) = cpuParams;
    std::tie(postOpMgrPtr, fusedOps) = fusingParams;

    std::vector<int> axes;
    CommonTestUtils::OpType opType;
    bool keepDims;
    ElementType inPrc, outPrc;
    std::vector<InputShape> inputShapes;

    std::tie(axes, opType, keepDims, reductionType, netPrecision, inPrc, outPrc, inputShapes, configuration) = basicParams;
    inPrc = outPrc = netPrecision;

    init_input_shapes(inputShapes);

    auto params = ngraph::builder::makeDynamicParams(netPrecision, inputDynamicShapes);
    auto paramOuts = ngraph::helpers::convert2OutputVector(
            ngraph::helpers::castOps2Nodes<ov::op::v0::Parameter>(params));

    std::vector<size_t> shapeAxes;
    switch (opType) {
        case CommonTestUtils::OpType::SCALAR:
            if (axes.size() > 1)
                FAIL() << "In reduce op if op type is scalar, 'axis' input's must contain 1 element";
            break;
        case CommonTestUtils::OpType::VECTOR:
            shapeAxes.push_back(axes.size());
            break;
        default:
            FAIL() << "Reduce op doesn't support operation type: " << opType;
    }
    auto reductionAxesNode = std::dynamic_pointer_cast<ov::Node>(
            std::make_shared<ov::op::v0::Constant>(ElementType::i64, ov::Shape(shapeAxes), axes));

    const auto reduce = ngraph::builder::makeReduce(paramOuts[0], reductionAxesNode, keepDims, reductionType);

    if (inPrc == ElementType::i64 || inPrc == ElementType::u64) {
        auto i64It = configuration.find(PluginConfigInternalParams::KEY_CPU_NATIVE_I64);
        if (i64It == configuration.end() || i64It->second == PluginConfigParams::NO) {
            selectedType = makeSelectedTypeStr(getPrimitiveType(), ElementType::i32);
        } else {
            selectedType = makeSelectedTypeStr(getPrimitiveType(), ElementType::i64);
        }
    } else if (inPrc == ElementType::boolean) {
        selectedType = makeSelectedTypeStr(getPrimitiveType(), ElementType::i8);
    } else {
        selectedType = makeSelectedTypeStr(getPrimitiveType(), inPrc);
    }

    // hybrid layouts
    if (inFmts.size() != 0 && outFmts.size() == 0) {
        size_t outShapeSize = inputDynamicShapes[0].size() - axes.size();
        switch (outShapeSize) {
            case 0:
            case 1:
                outFmts.push_back(x);
                break;
            case 2:
                outFmts.push_back(nc);
                break;
            case 3:
                outFmts.push_back(tnc);
                break;
            case 4:
                outFmts.push_back(nchw);
                break;
            default:
                FAIL() << "Invaid outShapeSize: " << outShapeSize;
        }
    }

    function = makeNgraphFunction(netPrecision, params, reduce, "Reduce");
}

void ReduceCPULayerTest::generate_inputs(const std::vector<ngraph::Shape>& targetInputStaticShapes) {
    inputs.clear();
    const auto& funcInputs = function->inputs();
    for (size_t i = 0; i < funcInputs.size(); ++i) {
        const auto& funcInput = funcInputs[i];
        ov::Tensor tensor;
        if (reductionType == ngraph::helpers::ReductionType::Prod) {
            tensor = utils::create_and_fill_tensor(funcInput.get_element_type(), targetInputStaticShapes[i], 10, 1);
            if (netPrecision == ElementType::f32) {
                auto *rawBlobDataPtr = static_cast<float *>(tensor.data());
                for (size_t i = 0; i < tensor.get_size(); ++i) {
                    rawBlobDataPtr[i] /= 10.f;
                }
            } else if (netPrecision == ElementType::bf16) {
                auto *rawBlobDataPtr = static_cast<ov::bfloat16 *>(tensor.data());
                for (size_t i = 0; i < tensor.get_size(); ++i) {
                    rawBlobDataPtr[i] /= 10.f;
                }
            } else if (netPrecision == ElementType::i64) {
            //     auto *rawBlobDataPtr = static_cast<int64_t *>(tensor.data());
            //     for (size_t i = 0; i < tensor.get_size(); ++i) {
            //         rawBlobDataPtr[i] /= 10;
            //     }
            }
        } else {
            tensor = utils::create_and_fill_tensor(funcInput.get_element_type(), targetInputStaticShapes[i]);
        }

        inputs.insert({funcInput.get_node_shared_ptr(), tensor});
    }
}

TEST_P(ReduceCPULayerTest, CompareWithRefs) {
    run();
    CheckPluginRelatedResults(compiledModel, "Reduce");
}

namespace Reduce {

const std::vector<bool>& keepDims() {
    static const std::vector<bool> keepDims = {
            true,
            false,
    };
    return keepDims;
}

const std::vector<std::vector<int>>& axes() {
    static const std::vector<std::vector<int>> axes = {
            {0},
            {1},
            {2},
            {3}
    };
    return axes;
}

const std::vector<std::vector<int>>& axesND() {
    static const std::vector<std::vector<int>> axesND = {
            {0, 1},
            {0, 2},
            {0, 3},
            {1, 2},
            {1, 3},
            {2, 3},
            {0, 1, 2},
            {0, 1, 3},
            {0, 2, 3},
            {1, 2, 3},
            {0, 1, 2, 3}
    };
    return axesND;
}

const std::vector<CommonTestUtils::OpType>& opTypes() {
    static const std::vector<CommonTestUtils::OpType> opTypes = {
            CommonTestUtils::OpType::SCALAR,
            CommonTestUtils::OpType::VECTOR,
    };
    return opTypes;
}

const std::vector<ngraph::helpers::ReductionType>& reductionTypes() {
    static const std::vector<ngraph::helpers::ReductionType> reductionTypes = {
            ngraph::helpers::ReductionType::Mean,
            ngraph::helpers::ReductionType::Max,
            ngraph::helpers::ReductionType::Sum,
            ngraph::helpers::ReductionType::Min,
            ngraph::helpers::ReductionType::Prod,
            ngraph::helpers::ReductionType::L1,
            ngraph::helpers::ReductionType::L2,
    };
    return reductionTypes;
}

const std::vector<ElementType>& inpOutPrc() {
    static const std::vector<ElementType> inpOutPrc = {ElementType::bf16, ElementType::f32};
    return inpOutPrc;
}

const std::vector<ngraph::helpers::ReductionType>& reductionTypesInt32() {
    static const std::vector<ngraph::helpers::ReductionType> reductionTypesInt32 = {
            ngraph::helpers::ReductionType::Sum,
            ngraph::helpers::ReductionType::Min,
            ngraph::helpers::ReductionType::Max,
            ngraph::helpers::ReductionType::L1,
    };
    return reductionTypesInt32;
}

}  // namespace Reduce
}  // namespace CPULayerTestsDefinitions