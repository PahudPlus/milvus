// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "db/Types.h"
#include "db/meta/MetaTypes.h"
#include "segment/DeletedDocs.h"
#include "segment/IdBloomFilter.h"
#include "segment/VectorIndex.h"

namespace milvus {
namespace engine {

using FIELD_TYPE = engine::meta::hybrid::DataType;
using FIELD_TYPE_MAP = std::unordered_map<std::string, engine::meta::hybrid::DataType>;
using FIELD_WIDTH_MAP = std::unordered_map<std::string, uint64_t>;
using FIXED_FIELD_DATA = std::vector<uint8_t>;
using FIXEDX_FIELD_MAP = std::unordered_map<std::string, FIXED_FIELD_DATA>;
using VARIABLE_FIELD_DATA = std::vector<std::string>;
using VARIABLE_FIELD_MAP = std::unordered_map<std::string, VARIABLE_FIELD_DATA>;
using VECTOR_INDEX_MAP = std::unordered_map<std::string, knowhere::VecIndexPtr>;

struct DataChunk {
    uint64_t count_ = 0;
    FIXEDX_FIELD_MAP fixed_fields_;
    VARIABLE_FIELD_MAP variable_fields_;
};

using DataChunkPtr = std::shared_ptr<DataChunk>;

class Segment {
 public:
    Status
    AddField(const std::string& field_name, FIELD_TYPE field_type, uint64_t field_width = 0);

    Status
    AddChunk(const DataChunkPtr& chunk_ptr);

    Status
    AddChunk(const DataChunkPtr& chunk_ptr, uint64_t from, uint64_t to);

    Status
    DeleteEntity(int32_t offset);

    Status
    GetFieldType(const std::string& field_name, FIELD_TYPE& type);

    Status
    GetFixedFieldWidth(const std::string& field_name, uint64_t width);

    Status
    GetFixedFieldData(const std::string& field_name, FIXED_FIELD_DATA& data);

    Status
    GetVectorIndex(const std::string& field_name, knowhere::VecIndexPtr& index);

    Status
    SetVectorIndex(const std::string& field_name, const knowhere::VecIndexPtr& index);

    FIELD_TYPE_MAP&
    GetFieldTypes() {
        return field_types_;
    }
    FIXEDX_FIELD_MAP&
    GetFixedFields() {
        return fixed_fields_;
    }
    VARIABLE_FIELD_MAP&
    GetVariableFields() {
        return variable_fields_;
    }
    VECTOR_INDEX_MAP&
    GetVectorIndice() {
        return vector_indice_;
    }

    int64_t
    GetRowCount() const {
        return row_count_;
    }

    segment::DeletedDocsPtr
    GetDeletedDocs() const {
        return deleted_docs_ptr_;
    }

    void
    SetDeletedDocs(const segment::DeletedDocsPtr& ptr) {
        deleted_docs_ptr_ = ptr;
    }

    segment::IdBloomFilterPtr
    GetBloomFilter() const {
        return id_bloom_filter_ptr_;
    }

    void
    SetBloomFilter(const segment::IdBloomFilterPtr& ptr) {
        id_bloom_filter_ptr_ = ptr;
    }

 private:
    FIELD_TYPE_MAP field_types_;
    FIELD_WIDTH_MAP fixed_fields_width_;
    FIXEDX_FIELD_MAP fixed_fields_;
    VARIABLE_FIELD_MAP variable_fields_;
    VECTOR_INDEX_MAP vector_indice_;

    uint64_t row_count_ = 0;

    segment::DeletedDocsPtr deleted_docs_ptr_ = nullptr;
    segment::IdBloomFilterPtr id_bloom_filter_ptr_ = nullptr;
};

using SegmentPtr = std::shared_ptr<Segment>;

}  // namespace engine
}  // namespace milvus
