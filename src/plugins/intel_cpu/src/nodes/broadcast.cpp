// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "broadcast.h"

#include "common/cpu_memcpy.h"
#include "ie_parallel.hpp"
#include <openvino/op/broadcast.hpp>
#include <openvino/op/constant.hpp>

using namespace InferenceEngine;

namespace ov {
namespace intel_cpu {
namespace node {

bool Broadcast::isSupportedOperation(const std::shared_ptr<const ov::Node>& op, std::string& errorMessage) noexcept {
    try {
        if (!ov::is_type<op::v1::Broadcast>(op)) {
            errorMessage = "Only Broadcast operations from opset1 are supported.";
            return false;
        }
        if (!one_of(ov::as_type_ptr<const op::v1::Broadcast>(op)->get_broadcast_spec().m_type,
                op::AutoBroadcastType::NUMPY, op::AutoBroadcastType::EXPLICIT)) {
            errorMessage = "Only NUMPY and EXPLICIT broadcast types are supported.";
            return false;
        }
        if (op->get_input_partial_shape(TARGET_SHAPE_IDX).is_dynamic() ||
                (op->get_input_size() > AXES_MAPPING_IDX && op->get_input_partial_shape(AXES_MAPPING_IDX).is_dynamic())) {
            errorMessage = "Only static shapes are supported for target shape and axes mapping inputs.";
            return false;
        }
        if (!isDynamicNgraphNode(op) &&
                (!ov::is_type<op::v0::Constant>(op->get_input_node_ptr(TARGET_SHAPE_IDX)) ||
                 (op->get_input_size() > AXES_MAPPING_IDX &&
                 !ov::is_type<op::v0::Constant>(op->get_input_node_ptr(AXES_MAPPING_IDX))))) {
            errorMessage = "Only constant target shapes and axis mapping inputs are supported for static shapes.";
            return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

Broadcast::Broadcast(const std::shared_ptr<ov::Node>& op, const GraphContext::CPtr context)
        : Node(op, context, NgraphShapeInferFactory(op, PortMask(TARGET_SHAPE_IDX, AXES_MAPPING_IDX))) {
    std::string errorMessage;
    if (!isSupportedOperation(op, errorMessage)) {
        IE_THROW(NotImplemented) << errorMessage;
    }

    errorPrefix = "Broadcast node with name '" + op->get_friendly_name() + "' ";
    if (op->get_input_size() != 2 && op->get_input_size() != 3)
        IE_THROW() << errorPrefix << "has incorrect number of input edges: " << getParentEdges().size();
    if (op->get_output_size() == 0)
        IE_THROW() << errorPrefix << "has no output edges.";

    auto broadcastOp = ov::as_type_ptr<const op::v1::Broadcast>(op);
    if (broadcastOp->get_broadcast_spec().m_type == op::AutoBroadcastType::NUMPY) {
        broadcastType = NUMPY;
    } else if (broadcastOp->get_broadcast_spec().m_type == op::AutoBroadcastType::EXPLICIT) {
        if (op->get_input_size() <= AXES_MAPPING_IDX)
            IE_THROW() << errorPrefix << " and EXPLICIT mode must have tree input edges: " << getParentEdges().size();
        broadcastType = EXPLICIT;
    } else {
        IE_THROW() << errorPrefix << "has unexpected broadcast type: " << broadcastOp->get_broadcast_spec().m_type;
    }

    if (auto shapeOp = ov::as_type<op::v0::Constant>(op->get_input_node_ptr(TARGET_SHAPE_IDX))) {
        constMap[TARGET_SHAPE_IDX] = true;
        targetShape = shapeOp->cast_vector<Dim>();
    }

    if (broadcastType == EXPLICIT) {
        if (auto axesOp = ov::as_type<op::v0::Constant>(op->get_input_node_ptr(AXES_MAPPING_IDX))) {
            constMap[AXES_MAPPING_IDX] = true;
            axesMapping = axesOp->cast_vector<Dim>();
        }
    }
}

void Broadcast::getSupportedDescriptors() {
    if (!isDynamicNode()) {
        const auto& srcDims = getInputShapeAtPort(INPUT_DATA_IDX).getDims();
        repeats.assign(targetShape.begin(), targetShape.end());
        const auto ndims = repeats.size();

        if (broadcastType == NUMPY) {
            for (size_t i = 0lu; i < srcDims.size(); i++) {
                repeats[ndims - 1lu - i] /= srcDims[srcDims.size() - 1lu - i];
            }
        } else if (broadcastType == EXPLICIT) {
            for (size_t i = 0lu; i < axesMapping.size(); i++) {
                repeats[axesMapping[i]] /= srcDims[i];
            }
        }
        needPrepareParamsVar = true;
    }
}

void Broadcast::initSupportedPrimitiveDescriptors() {
    if (!supportedPrimitiveDescriptors.empty())
        return;

    supportedPrimitiveDescriptors = getSupportedConfigs(this);
}

bool Broadcast::needPrepareParams() const {
    return needPrepareParamsVar;
}

void Broadcast::prepareParams() {
    if (!constMap[TARGET_SHAPE_IDX]) {
        const auto& targetShapeMem = getParentEdgesAtPort(TARGET_SHAPE_IDX)[0]->getMemory();
        if (targetShapeMem.getDataType() == dnnl::memory::data_type::s64) {
            const auto *targetShapeData = reinterpret_cast<const int64_t *>(targetShapeMem.getData());
            targetShape.assign(targetShapeData, targetShapeData + targetShapeMem.getStaticDims()[0]);
        } else if (targetShapeMem.getDataType() == dnnl::memory::data_type::s32) {
            const auto *targetShapeData = reinterpret_cast<const int32_t *>(targetShapeMem.getData());
            targetShape.assign(targetShapeData, targetShapeData + targetShapeMem.getStaticDims()[0]);
        } else {
            IE_THROW() << errorPrefix << " does not support precision '" << int(targetShapeMem.getDataType())
                       << "' for the Target shape input.";
        }
    }
    if (broadcastType == EXPLICIT && !constMap[AXES_MAPPING_IDX]) {
        const auto& axesMapMem = getParentEdgesAtPort(AXES_MAPPING_IDX)[0]->getMemory();
        if (axesMapMem.getDataType() == dnnl::memory::data_type::s64) {
            const auto axesMapData = reinterpret_cast<const int64_t *>(axesMapMem.getData());
            axesMapping.assign(axesMapData, axesMapData + axesMapMem.getStaticDims()[0]);
        } else if (axesMapMem.getDataType() == dnnl::memory::data_type::s32) {
            const auto axesMapData = reinterpret_cast<const int32_t *>(axesMapMem.getData());
            axesMapping.assign(axesMapData, axesMapData + axesMapMem.getStaticDims()[0]);
        } else {
            IE_THROW() << errorPrefix << " does not support precision '" << int(axesMapMem.getDataType())
                       << "' for the Axes mapping input.";
        }
    }

    const auto& srcDims = getParentEdgesAtPort(INPUT_DATA_IDX)[0]->getMemory().getShape().getStaticDims();
    repeats.assign(targetShape.begin(), targetShape.end());
    const auto ndims = repeats.size();

    auto srcBlockedDims = getParentEdgeAt(INPUT_DATA_IDX)->getMemory().getDescWithType<BlockedMemoryDesc>()->getBlockDims();
    auto dstBlockedDims = getChildEdgeAt(0)->getMemory().getDescWithType<BlockedMemoryDesc>()->getBlockDims();

    if (broadcastType == NUMPY) {
        for (size_t i = 0lu; i < srcDims.size(); i++) {
            repeats[ndims - 1lu - i] /= srcDims[srcDims.size() - 1lu - i];
        }
    } else if (broadcastType == EXPLICIT) {
        for (size_t i = 0; i < getInputShapeAtPort(AXES_MAPPING_IDX).getDims()[0]; i++) {
            repeats[axesMapping[i]] /= srcDims[i];
        }

        SizeVector newSrcBlockedDims = SizeVector(dstBlockedDims.size(), 1);
        for (size_t i = 0; i < getInputShapeAtPort(AXES_MAPPING_IDX).getDims()[0]; i++) {
            newSrcBlockedDims[axesMapping[i]] = srcBlockedDims[i];
        }
        srcBlockedDims = newSrcBlockedDims;
    }

    optimizedCase = prepareOptimizedParams(this, srcBlockedDims, dstBlockedDims);
}

bool Broadcast::needShapeInfer() const {
    needPrepareParamsVar = true;
    if (inputShapesModified()) {
        return true;
    }

    if (!constMap[TARGET_SHAPE_IDX]) {
        if (targetShape.empty()) {
            return true;
        }
        const auto& targetShapeMem = getParentEdgesAtPort(TARGET_SHAPE_IDX)[0]->getMemory();
        if (targetShapeMem.getDataType() == dnnl::memory::data_type::s64) {
            const auto *targetShapeData = reinterpret_cast<const int64_t *>(targetShapeMem.getData());
            for (size_t i = 0lu; i < targetShape.size(); i++) {
                if (targetShape[i] != targetShapeData[i]) {
                    return true;
                }
            }
        } else if (targetShapeMem.getDataType() == dnnl::memory::data_type::s32) {
            const auto *targetShapeData = reinterpret_cast<const int32_t *>(targetShapeMem.getData());
            for (size_t i = 0lu; i < targetShape.size(); i++) {
                if (targetShape[i] != targetShapeData[i]) {
                    return true;
                }
            }
        } else {
            IE_THROW() << errorPrefix << " does not support precision '" << int(targetShapeMem.getDataType())
                       << "' for the Target shape input.";
        }
    }
    if (broadcastType == EXPLICIT && !constMap[AXES_MAPPING_IDX]) {
        if (axesMapping.empty()) {
            return true;
        }
        const auto& axesMapMem = getParentEdgesAtPort(AXES_MAPPING_IDX)[0]->getMemory();
        if (axesMapMem.getDataType() == dnnl::memory::data_type::s64) {
            const auto *axesMappingData = reinterpret_cast<const int64_t *>(axesMapMem.getData());
            for (size_t i = 0lu; i < axesMapping.size(); i++) {
                if (axesMapping[i] != axesMappingData[i]) {
                    return true;
                }
            }
        } else if (axesMapMem.getDataType() == dnnl::memory::data_type::s32) {
            const auto *axesMappingData = reinterpret_cast<const int32_t *>(axesMapMem.getData());
            for (size_t i = 0lu; i < axesMapping.size(); i++) {
                if (axesMapping[i] != axesMappingData[i]) {
                    return true;
                }
            }
        } else {
            IE_THROW() << errorPrefix << " does not support precision '" << int(axesMapMem.getDataType())
                       << "' for the Axes mapping input.";
        }
    }
    needPrepareParamsVar = false;
    return false;
}

bool Broadcast::isExecutable() const {
    return !isInputTensorAtPortEmpty(0);
}

void Broadcast::executeDynamicImpl(dnnl::stream strm) {
    execute(strm);
}

void Broadcast::execute(dnnl::stream strm) {
    if (optimizedCase) {
        optimizedExecute(getParentEdgeAt(INPUT_DATA_IDX)->getMemoryPtr(), getChildEdgeAt(0)->getMemoryPtr());
    } else {
        plainExecute(strm);
    }
}

void Broadcast::plainExecute(dnnl::stream strm) {
    VectorDims srcDims = getParentEdgeAt(INPUT_DATA_IDX)->getMemory().getStaticDims();
    const auto& dstDims = getChildEdgeAt(0)->getMemory().getStaticDims();
    const auto& dataSrcRank = getParentEdgeAt(INPUT_DATA_IDX)->getMemory().getShape().getRank();
    const auto& dataDstRank = getChildEdgeAt(0)->getMemory().getShape().getRank();

    auto srcDesc = getParentEdgeAt(INPUT_DATA_IDX)->getMemory().getDescWithType<BlockedMemoryDesc>();
    VectorDims srcStrides = srcDesc->getStrides();
    const size_t dataSize = srcDesc->getPrecision().size();

    if (!dataSrcRank)
        srcDims = VectorDims(1, 1);
    if (!srcStrides.size())
        srcStrides = VectorDims(1, 1);

    auto dstDesc = getChildEdgeAt(0)->getMemory().getDescWithType<BlockedMemoryDesc>();
    VectorDims dstStrides = dstDesc->getStrides();
    VectorDims srcAligned(dataDstRank);
    VectorDims srcStridesAligned(dataDstRank);
    const size_t prefixSize = dataDstRank - dataSrcRank;
    for (size_t i = 0lu; i < dataDstRank; i++) {
        if (i < prefixSize) {
            srcAligned[i] = 1;
            srcStridesAligned[i] = srcStrides[0];
        } else {
            srcAligned[i] = srcDims[i - prefixSize];
            srcStridesAligned[i] = srcStrides[i - prefixSize];
        }
    }

    const size_t workAmountDst = dstStrides[0] * dstDims[0];
    const auto *srcData = reinterpret_cast<const uint8_t *>(getParentEdgeAt(INPUT_DATA_IDX)->getMemoryPtr()->getData());
    auto *dstData = reinterpret_cast<uint8_t *>(getChildEdgeAt(0)->getMemoryPtr()->getData());

    parallel_nt(0, [&](const int ithr, const int nthr) {
        size_t i = 0lu, srcIdx = 0lu, start = 0lu, end = 0lu;
        VectorDims counters(dataDstRank, 0);
        splitter(workAmountDst, nthr, ithr, start, end);
        for (int j = dataDstRank - 1, i = start; j >= 0; j--) {
            counters[j] = i % dstDims[j];
            i /= dstDims[j];
        }
        for (size_t iwork = start * dataSize; iwork < end * dataSize; iwork += dataSize) {
            for (i = 0lu, srcIdx = 0lu; i < dataDstRank; ++i)
                srcIdx += counters[i] ? ((counters[i] % srcAligned[i]) * srcStridesAligned[i]) : 0;

            cpu_memcpy(&dstData[iwork], &srcData[srcIdx * dataSize], dataSize);

            for (int j = dataDstRank - 1; j >= 0; j--) {
                counters[j] = (counters[j] + 1) % dstDims[j];
                if (counters[j] != 0) break;
            }
        }
    });
}

bool Broadcast::created() const {
    return getType() == Type::Broadcast;
}

}   // namespace node
}   // namespace intel_cpu
}   // namespace ov
