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

#include "db/meta/backend/MySqlEngine.h"

#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fiu-local.h>

#include "db/Utils.h"
#include "db/meta/MetaFields.h"
#include "db/meta/backend/MetaHelper.h"
#include "utils/Exception.h"
#include "utils/StringHelpFunctions.h"

namespace milvus::engine::meta {

////////// private namespace //////////
namespace {
class MetaField {
 public:
    MetaField(const std::string& name, const std::string& type, const std::string& setting)
        : name_(name), type_(type), setting_(setting) {
    }

    std::string
    name() const {
        return name_;
    }

    std::string
    ToString() const {
        return name_ + " " + type_ + " " + setting_;
    }

    // mysql field type has additional information. for instance, a filed type is defined as 'BIGINT'
    // we get the type from sql is 'bigint(20)', so we need to ignore the '(20)'
    bool
    IsEqual(const MetaField& field) const {
        size_t name_len_min = field.name_.length() > name_.length() ? name_.length() : field.name_.length();
        size_t type_len_min = field.type_.length() > type_.length() ? type_.length() : field.type_.length();

        // only check field type, don't check field width, for example: VARCHAR(255) and VARCHAR(100) is equal
        std::vector<std::string> type_split;
        milvus::StringHelpFunctions::SplitStringByDelimeter(type_, "(", type_split);
        if (!type_split.empty()) {
            type_len_min = type_split[0].length() > type_len_min ? type_len_min : type_split[0].length();
        }

        // field name must be equal, ignore type width
        return strncasecmp(field.name_.c_str(), name_.c_str(), name_len_min) == 0 &&
               strncasecmp(field.type_.c_str(), type_.c_str(), type_len_min) == 0;
    }

 private:
    std::string name_;
    std::string type_;
    std::string setting_;
};

using MetaFields = std::vector<MetaField>;

class MetaSchema {
 public:
    MetaSchema(const std::string& name, const MetaFields& fields) : name_(name), fields_(fields), constraint_fields_() {
    }

    MetaSchema(const std::string& name, const MetaFields& fields, const MetaFields& constraints)
        : name_(name), fields_(fields), constraint_fields_(constraints) {
    }

    std::string
    name() const {
        return name_;
    }

    std::string
    ToString() const {
        std::string result;
        for (auto& field : fields_) {
            if (!result.empty()) {
                result += ",";
            }
            result += field.ToString();
        }

        std::string constraints;
        for (auto& constraint : constraint_fields_) {
            if (!constraints.empty()) {
                constraints += ",";
            }
            constraints += constraint.name();
        }

        if (!constraints.empty()) {
            result += ",constraint uq unique(" + constraints + ")";
        }

        return result;
    }

    // if the outer fields contains all this MetaSchema fields, return true
    // otherwise return false
    bool
    IsEqual(const MetaFields& fields) const {
        std::vector<std::string> found_field;
        for (const auto& this_field : fields_) {
            for (const auto& outer_field : fields) {
                if (this_field.IsEqual(outer_field)) {
                    found_field.push_back(this_field.name());
                    break;
                }
            }
        }

        return found_field.size() == fields_.size();
    }

 private:
    std::string name_;
    MetaFields fields_;
    MetaFields constraint_fields_;
};

static const MetaField MetaIdField = MetaField(F_ID, "BIGINT", "PRIMARY KEY AUTO_INCREMENT");
static const MetaField MetaCollectionIdField = MetaField(F_COLLECTON_ID, "BIGINT", "NOT NULL");
static const MetaField MetaPartitionIdField = MetaField(F_PARTITION_ID, "BIGINT", "NOT NULL");
static const MetaField MetaSchemaIdField = MetaField(F_SCHEMA_ID, "BIGINT", "NOT NULL");
static const MetaField MetaSegmentIdField = MetaField(F_SEGMENT_ID, "BIGINT", "NOT NULL");
static const MetaField MetaFieldElementIdField = MetaField(F_FIELD_ELEMENT_ID, "BIGINT", "NOT NULL");
static const MetaField MetaFieldIdField = MetaField(F_FIELD_ID, "BIGINT", "NOT NULL");
static const MetaField MetaNameField = MetaField(F_NAME, "VARCHAR(255)", "NOT NULL");
static const MetaField MetaMappingsField = MetaField(F_MAPPINGS, "JSON", "NOT NULL");
static const MetaField MetaNumField = MetaField(F_NUM, "BIGINT", "NOT NULL");
static const MetaField MetaLSNField = MetaField(F_LSN, "BIGINT", "NOT NULL");
static const MetaField MetaFtypeField = MetaField(F_FTYPE, "BIGINT", "NOT NULL");
static const MetaField MetaStateField = MetaField(F_STATE, "TINYINT", "NOT NULL");
static const MetaField MetaCreatedOnField = MetaField(F_CREATED_ON, "BIGINT", "NOT NULL");
static const MetaField MetaUpdatedOnField = MetaField(F_UPDATED_ON, "BIGINT", "NOT NULL");
static const MetaField MetaParamsField = MetaField(F_PARAMS, "JSON", "NOT NULL");
static const MetaField MetaSizeField = MetaField(F_SIZE, "BIGINT", "NOT NULL");
static const MetaField MetaRowCountField = MetaField(F_ROW_COUNT, "BIGINT", "NOT NULL");

// Environment schema
static const MetaSchema COLLECTION_SCHEMA(snapshot::Collection::Name,
                                          {MetaIdField, MetaNameField, MetaLSNField, MetaParamsField, MetaStateField,
                                           MetaCreatedOnField, MetaUpdatedOnField});

// Tables schema
static const MetaSchema COLLECTIONCOMMIT_SCHEMA(snapshot::CollectionCommit::Name,
                                                {MetaIdField, MetaCollectionIdField, MetaSchemaIdField,
                                                 MetaMappingsField, MetaRowCountField, MetaSizeField, MetaLSNField,
                                                 MetaStateField, MetaCreatedOnField, MetaUpdatedOnField});

// TableFiles schema
static const MetaSchema PARTITION_SCHEMA(snapshot::Partition::Name,
                                         {MetaIdField, MetaNameField, MetaCollectionIdField, MetaLSNField,
                                          MetaStateField, MetaCreatedOnField, MetaUpdatedOnField});

// Fields schema
static const MetaSchema PARTITIONCOMMIT_SCHEMA(snapshot::PartitionCommit::Name,
                                               {MetaIdField, MetaCollectionIdField, MetaPartitionIdField,
                                                MetaMappingsField, MetaRowCountField, MetaSizeField, MetaStateField,
                                                MetaLSNField, MetaCreatedOnField, MetaUpdatedOnField});

static const MetaSchema SEGMENT_SCHEMA(snapshot::Segment::Name, {
                                                                    MetaIdField,
                                                                    MetaCollectionIdField,
                                                                    MetaPartitionIdField,
                                                                    MetaNumField,
                                                                    MetaLSNField,
                                                                    MetaStateField,
                                                                    MetaCreatedOnField,
                                                                    MetaUpdatedOnField,
                                                                });

static const MetaSchema SEGMENTCOMMIT_SCHEMA(snapshot::SegmentCommit::Name, {
                                                                                MetaIdField,
                                                                                MetaSchemaIdField,
                                                                                MetaPartitionIdField,
                                                                                MetaSegmentIdField,
                                                                                MetaMappingsField,
                                                                                MetaRowCountField,
                                                                                MetaSizeField,
                                                                                MetaLSNField,
                                                                                MetaStateField,
                                                                                MetaCreatedOnField,
                                                                                MetaUpdatedOnField,
                                                                            });

static const MetaSchema SEGMENTFILE_SCHEMA(snapshot::SegmentFile::Name,
                                           {MetaIdField, MetaCollectionIdField, MetaPartitionIdField,
                                            MetaSegmentIdField, MetaFieldElementIdField, MetaRowCountField,
                                            MetaSizeField, MetaLSNField, MetaStateField, MetaCreatedOnField,
                                            MetaUpdatedOnField});

static const MetaSchema SCHEMACOMMIT_SCHEMA(snapshot::SchemaCommit::Name, {
                                                                              MetaIdField,
                                                                              MetaCollectionIdField,
                                                                              MetaMappingsField,
                                                                              MetaLSNField,
                                                                              MetaStateField,
                                                                              MetaCreatedOnField,
                                                                              MetaUpdatedOnField,
                                                                          });

static const MetaSchema FIELD_SCHEMA(snapshot::Field::Name,
                                     {MetaIdField, MetaNameField, MetaNumField, MetaFtypeField, MetaParamsField,
                                      MetaLSNField, MetaStateField, MetaCreatedOnField, MetaUpdatedOnField});

static const MetaSchema FIELDCOMMIT_SCHEMA(snapshot::FieldCommit::Name,
                                           {MetaIdField, MetaCollectionIdField, MetaFieldIdField, MetaMappingsField,
                                            MetaLSNField, MetaStateField, MetaCreatedOnField, MetaUpdatedOnField});

static const MetaSchema FIELDELEMENT_SCHEMA(snapshot::FieldElement::Name,
                                            {MetaIdField, MetaCollectionIdField, MetaFieldIdField, MetaNameField,
                                             MetaFtypeField, MetaParamsField, MetaLSNField, MetaStateField,
                                             MetaCreatedOnField, MetaUpdatedOnField});

}  // namespace

/////////////// MySqlEngine ///////////////
Status
MySqlEngine::Initialize() {
    // step 1: create db root path
    //    if (!boost::filesystem::is_directory(options_.path_)) {
    //        auto ret = boost::filesystem::create_directory(options_.path_);
    //        fiu_do_on("MySQLMetaImpl.Initialize.fail_create_directory", ret = false);
    //        if (!ret) {
    //            std::string msg = "Failed to create db directory " + options_.path_;
    //            LOG_ENGINE_ERROR_ << msg;
    //            throw Exception(DB_META_TRANSACTION_FAILED, msg);
    //        }
    //    }
    std::string uri = options_.backend_uri_;

    // step 2: parse and check meta uri
    utils::MetaUriInfo uri_info;
    auto status = utils::ParseMetaUri(uri, uri_info);
    if (!status.ok()) {
        std::string msg = "Wrong URI format: " + uri;
        LOG_ENGINE_ERROR_ << msg;
        throw Exception(DB_INVALID_META_URI, msg);
    }

    if (strcasecmp(uri_info.dialect_.c_str(), "mysql") != 0) {
        std::string msg = "URI's dialect is not MySQL";
        LOG_ENGINE_ERROR_ << msg;
        throw Exception(DB_INVALID_META_URI, msg);
    }

    // step 3: connect mysql
    unsigned int thread_hint = std::thread::hardware_concurrency();
    int max_pool_size = (thread_hint > 8) ? static_cast<int>(thread_hint) : 8;
    int port = 0;
    if (!uri_info.port_.empty()) {
        port = std::stoi(uri_info.port_);
    }

    mysql_connection_pool_ = std::make_shared<meta::MySQLConnectionPool>(
        uri_info.db_name_, uri_info.username_, uri_info.password_, uri_info.host_, port, max_pool_size);
    LOG_ENGINE_DEBUG_ << "MySQL connection pool: maximum pool size = " << std::to_string(max_pool_size);

    // step 4: validate to avoid open old version schema
    //    ValidateMetaSchema();

    // step 5: clean shadow files
    //    if (mode_ != DBOptions::MODE::CLUSTER_READONLY) {
    //        CleanUpShadowFiles();
    //    }

    // step 6: try connect mysql server
    mysqlpp::ScopedConnection connectionPtr(*mysql_connection_pool_, safe_grab_);

    if (connectionPtr == nullptr) {
        std::string msg = "Failed to connect MySQL meta server: " + uri;
        LOG_ENGINE_ERROR_ << msg;
        throw Exception(DB_INVALID_META_URI, msg);
    }

    bool is_thread_aware = connectionPtr->thread_aware();
    fiu_do_on("MySQLMetaImpl.Initialize.is_thread_aware", is_thread_aware = false);
    if (!is_thread_aware) {
        std::string msg =
            "Failed to initialize MySQL meta backend: MySQL client component wasn't built with thread awareness";
        LOG_ENGINE_ERROR_ << msg;
        throw Exception(DB_INVALID_META_URI, msg);
    }

    mysqlpp::Query InitializeQuery = connectionPtr->query();

    auto create_schema = [&](const MetaSchema& schema) {
        std::string create_table_str = "CREATE TABLE IF NOT EXISTS " + schema.name() + "(" + schema.ToString() + ");";
        InitializeQuery << create_table_str;
        //        LOG_ENGINE_DEBUG_ << "Initialize: " << InitializeQuery.str();

        bool initialize_query_exec = InitializeQuery.exec();
        //        fiu_do_on("MySQLMetaImpl.Initialize.fail_create_collection_files", initialize_query_exec = false);
        if (!initialize_query_exec) {
            std::string msg = "Failed to create meta collection '" + schema.name() + "' in MySQL";
            LOG_ENGINE_ERROR_ << msg;
            throw Exception(DB_META_TRANSACTION_FAILED, msg);
        }
        sleep(1);
    };

    create_schema(COLLECTION_SCHEMA);
    create_schema(COLLECTIONCOMMIT_SCHEMA);
    create_schema(PARTITION_SCHEMA);
    create_schema(PARTITIONCOMMIT_SCHEMA);
    create_schema(SEGMENT_SCHEMA);
    create_schema(SEGMENTCOMMIT_SCHEMA);
    create_schema(SEGMENTFILE_SCHEMA);
    create_schema(SCHEMACOMMIT_SCHEMA);
    create_schema(FIELD_SCHEMA);
    create_schema(FIELDCOMMIT_SCHEMA);
    create_schema(FIELDELEMENT_SCHEMA);

    return Status::OK();
}

Status
MySqlEngine::Query(const MetaQueryContext& context, AttrsMapList& attrs) {
    try {
        mysqlpp::ScopedConnection connectionPtr(*mysql_connection_pool_, safe_grab_);

        std::string sql;
        auto status = MetaHelper::MetaQueryContextToSql(context, sql);
        if (!status.ok()) {
            return status;
        }

        std::lock_guard<std::mutex> lock(meta_mutex_);

        mysqlpp::Query query = connectionPtr->query(sql);
        auto res = query.store();
        if (!res) {
            throw Exception(1, "Query res is false");
        }

        auto names = res.field_names();
        for (auto& row : res) {
            AttrsMap attrs_map;
            for (auto& name : *names) {
                attrs_map.insert(std::pair<std::string, std::string>(name, row[name.c_str()]));
            }
            attrs.push_back(attrs_map);
        }
    } catch (const mysqlpp::BadQuery& er) {
        // Handle any query errors
        //        cerr << "Query error: " << er.what() << endl;
        return Status(1, er.what());
    } catch (const mysqlpp::BadConversion& er) {
        // Handle bad conversions
        //        cerr << "Conversion error: " << er.what() << endl <<
        //             "\tretrieved data size: " << er.retrieved <<
        //             ", actual size: " << er.actual_size << endl;
        return Status(1, er.what());
    } catch (const mysqlpp::Exception& er) {
        // Catch-all for any other MySQL++ exceptions
        //        cerr << "Error: " << er.what() << endl;
        return Status(1, er.what());
    }

    return Status::OK();
}

Status
MySqlEngine::ExecuteTransaction(const std::vector<MetaApplyContext>& sql_contexts, std::vector<int64_t>& result_ids) {
    try {
        mysqlpp::ScopedConnection connectionPtr(*mysql_connection_pool_, safe_grab_);
        mysqlpp::Transaction trans(*connectionPtr, mysqlpp::Transaction::serializable, mysqlpp::Transaction::session);

        std::lock_guard<std::mutex> lock(meta_mutex_);
        for (auto& context : sql_contexts) {
            std::string sql;
            auto status = MetaHelper::MetaApplyContextToSql(context, sql);
            if (!status.ok()) {
                return status;
            }

            auto query = connectionPtr->query(sql);
            auto res = query.execute();
            if (context.op_ == oAdd) {
                auto id = res.insert_id();
                result_ids.push_back(id);
            } else {
                result_ids.push_back(context.id_);
            }
        }

        trans.commit();
        //        std::cout << "[DB] Transaction commit " << std::endl;
    } catch (const mysqlpp::BadQuery& er) {
        // Handle any query errors
        //        cerr << "Query error: " << er.what() << endl;
        //        return -1;
        //        std::cout << "[DB] Error: " << er.what() << std::endl;
        return Status(SERVER_UNSUPPORTED_ERROR, er.what());
    } catch (const mysqlpp::BadConversion& er) {
        // Handle bad conversions
        //        cerr << "Conversion error: " << er.what() << endl <<
        //             "\tretrieved data size: " << er.retrieved <<
        //             ", actual size: " << er.actual_size << endl;
        //        return -1;
        //        std::cout << "[DB] Error: " << er.what() << std::endl;
        return Status(SERVER_UNSUPPORTED_ERROR, er.what());
    } catch (const mysqlpp::Exception& er) {
        // Catch-all for any other MySQL++ exceptions
        //        cerr << "Error: " << er.what() << endl;
        //        return -1;
        //        std::cout << "[DB] Error: " << er.what() << std::endl;
        return Status(SERVER_UNSUPPORTED_ERROR, er.what());
    }

    return Status::OK();
}

Status
MySqlEngine::TruncateAll() {
    static std::vector<std::string> collecton_names = {
        COLLECTION_SCHEMA.name(),      COLLECTIONCOMMIT_SCHEMA.name(), PARTITION_SCHEMA.name(),
        PARTITIONCOMMIT_SCHEMA.name(), SEGMENT_SCHEMA.name(),          SEGMENTCOMMIT_SCHEMA.name(),
        SEGMENTFILE_SCHEMA.name(),     SCHEMACOMMIT_SCHEMA.name(),     FIELD_SCHEMA.name(),
        FIELDCOMMIT_SCHEMA.name(),     FIELDELEMENT_SCHEMA.name(),
    };

    std::vector<MetaApplyContext> contexts;
    for (auto& name : collecton_names) {
        MetaApplyContext context;
        context.sql_ = "TRUNCATE " + name + ";";
        context.id_ = 0;

        contexts.push_back(context);
    }

    std::vector<snapshot::ID_TYPE> ids;
    return ExecuteTransaction(contexts, ids);
}

}  // namespace milvus::engine::meta
