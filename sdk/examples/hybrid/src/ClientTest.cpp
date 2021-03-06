// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License.

#include "examples/hybrid/src/ClientTest.h"
#include "examples/utils/TimeRecorder.h"
#include "examples/utils/Utils.h"
#include "include/BooleanQuery.h"
#include "include/MilvusApi.h"

#include <unistd.h>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

const char* COLLECTION_NAME = milvus_sdk::Utils::GenCollectionName().c_str();

constexpr int64_t COLLECTION_DIMENSION = 128;
constexpr int64_t COLLECTION_INDEX_FILE_SIZE = 1024;
constexpr milvus::MetricType COLLECTION_METRIC_TYPE = milvus::MetricType::L2;
constexpr int64_t BATCH_ENTITY_COUNT = 100000;
constexpr int64_t NQ = 5;
constexpr int64_t TOP_K = 10;
constexpr int64_t NPROBE = 32;
constexpr int64_t SEARCH_TARGET = BATCH_ENTITY_COUNT / 2;  // change this value, result is different
constexpr int64_t ADD_ENTITY_LOOP = 5;
constexpr milvus::IndexType INDEX_TYPE = milvus::IndexType::IVFSQ8;
constexpr int32_t NLIST = 16384;
constexpr uint64_t FIELD_NUM = 3;

void
PrintHybridQueryResult(const std::vector<int64_t>& id_array, const milvus::HybridQueryResult& result) {
    for (size_t i = 0; i < id_array.size(); i++) {
        std::string prefix = "No." + std::to_string(i) + " id:" + std::to_string(id_array[i]);
        std::cout << prefix << "\t[";
        for (size_t j = 0; j < result.attr_records.size(); i++) {
        }
    }
}

}  // namespace

ClientTest::ClientTest(const std::string& address, const std::string& port) {
    milvus::ConnectParam param = {address, port};
    conn_ = milvus::Connection::Create();
    milvus::Status stat = conn_->Connect(param);
    std::cout << "Connect function call status: " << stat.message() << std::endl;
}

ClientTest::~ClientTest() {
    milvus::Status stat = milvus::Connection::Destroy(conn_);
    std::cout << "Destroy connection function call status: " << stat.message() << std::endl;
}

void
ClientTest::CreateHybridCollection(const std::string& collection_name) {
    milvus::FieldPtr field_ptr1 = std::make_shared<milvus::Field>();
    milvus::FieldPtr field_ptr2 = std::make_shared<milvus::Field>();
    milvus::VectorFieldPtr vec_field_ptr = std::make_shared<milvus::VectorField>();
    field_ptr1->field_type = milvus::DataType::INT64;
    field_ptr1->field_name = "field_1";
    field_ptr2->field_type = milvus::DataType::FLOAT;
    field_ptr2->field_name = "field_2";
    vec_field_ptr->field_type = milvus::DataType::VECTOR;
    vec_field_ptr->field_name = "field_3";
    vec_field_ptr->dimension = 128;

    std::vector<milvus::FieldPtr> numerica_fields;
    std::vector<milvus::VectorFieldPtr> vector_fields;
    numerica_fields.emplace_back(field_ptr1);
    numerica_fields.emplace_back(field_ptr2);
    vector_fields.emplace_back(vec_field_ptr);

    milvus::HMapping mapping = {collection_name, numerica_fields, vector_fields};
    milvus::Status stat = conn_->CreateHybridCollection(mapping);
    std::cout << "CreateHybridCollection function call status: " << stat.message() << std::endl;
}

void
ClientTest::Flush(const std::string& collection_name) {
    milvus_sdk::TimeRecorder rc("Flush");
    std::vector<std::string> collections = {collection_name};
    milvus::Status stat = conn_->Flush(collections);
    std::cout << "Flush function call status: " << stat.message() << std::endl;
}

void
ClientTest::InsertHybridEntities(std::string& collection_name, int64_t row_num) {
    std::unordered_map<std::string, std::vector<int64_t>> numerica_int_value;
    std::unordered_map<std::string, std::vector<double>> numerica_double_value;
    std::vector<int64_t> value1;
    std::vector<double> value2;
    value1.resize(row_num);
    value2.resize(row_num);
    for (uint64_t i = 0; i < row_num; ++i) {
        value1[i] = i;
        value2[i] = (double)(i + row_num);
    }

    numerica_int_value.insert(std::make_pair("field_1", value1));
    numerica_double_value.insert(std::make_pair("field_2", value2));

    std::unordered_map<std::string, std::vector<milvus::Entity>> vector_value;
    std::vector<milvus::Entity> entity_array;
    std::vector<int64_t> record_ids;
    {  // generate vectors
        milvus_sdk::Utils::BuildEntities(0, row_num, entity_array, record_ids, 128);
    }

    vector_value.insert(std::make_pair("field_3", entity_array));
    milvus::HEntity entity = {row_num, numerica_int_value, numerica_double_value, vector_value};
    std::vector<uint64_t> id_array;
    milvus::Status status = conn_->InsertEntity(collection_name, "", entity, id_array);
    std::cout << "InsertHybridEntities function call status: " << status.message() << std::endl;
}

void
ClientTest::HybridSearchPB(std::string& collection_name) {
    std::vector<std::string> partition_tags;
    milvus::TopKHybridQueryResult topk_query_result;

    auto leaf_queries = milvus_sdk::Utils::GenLeafQuery();

    // must
    auto must_clause = std::make_shared<milvus::BooleanQuery>(milvus::Occur::MUST);
    must_clause->AddLeafQuery(leaf_queries[0]);
    must_clause->AddLeafQuery(leaf_queries[1]);
    must_clause->AddLeafQuery(leaf_queries[2]);

    auto query_clause = std::make_shared<milvus::BooleanQuery>();
    query_clause->AddBooleanQuery(must_clause);

    std::string extra_params;
    milvus::Status status =
        conn_->HybridSearchPB(collection_name, partition_tags, query_clause, extra_params, topk_query_result);

    milvus_sdk::Utils::PrintTopKHybridQueryResult(topk_query_result);
    std::cout << "HybridSearch function call status: " << status.message() << std::endl;
}

void
ClientTest::HybridSearch(std::string& collection_name) {
    nlohmann::json dsl_json, vector_param_json;
    milvus_sdk::Utils::GenDSLJson(dsl_json, vector_param_json);

    std::vector<milvus::Entity> entity_array;
    std::vector<int64_t> record_ids;
    {  // generate vectors
        milvus_sdk::Utils::ConstructVector(NQ, COLLECTION_DIMENSION, entity_array);
    }

    std::vector<std::string> partition_tags;
    milvus::TopKHybridQueryResult topk_query_result;
    auto status = conn_->HybridSearch(collection_name, partition_tags, dsl_json.dump(), vector_param_json.dump(),
                                      entity_array, topk_query_result);

    milvus_sdk::Utils::PrintTopKHybridQueryResult(topk_query_result);
    std::cout << "HybridSearch function call status: " << status.message() << std::endl;
}

void
ClientTest::GetHEntityByID(const std::string& collection_name, const std::vector<int64_t>& id_array) {
    milvus::HybridQueryResult result;
    {
        milvus_sdk::TimeRecorder rc("GetHybridEntityByID");
        milvus::Status stat = conn_->GetHEntityByID(collection_name, id_array, result);
        std::cout << "GetEntitiesByID function call status: " << stat.message() << std::endl;
    }

    PrintHybridQueryResult(id_array, result);
}

void
ClientTest::TestHybrid() {
    std::string collection_name = "HYBRID_TEST";
    CreateHybridCollection(collection_name);
    InsertHybridEntities(collection_name, 10000);
    Flush(collection_name);
    sleep(2);
    //    HybridSearchPB(collection_name);
    HybridSearch(collection_name);
}
