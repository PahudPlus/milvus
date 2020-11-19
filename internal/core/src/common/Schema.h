#pragma once
#include "FieldMeta.h"
#include <vector>

namespace milvus {

class Schema {
 public:
    void
    AddField(std::string_view field_name, DataType data_type, int dim = 1) {
        auto field_meta = FieldMeta(field_name, data_type, dim);
        this->AddField(std::move(field_meta));
    }

    void
    AddField(FieldMeta field_meta) {
        auto offset = fields_.size();
        fields_.emplace_back(field_meta);
        offsets_.emplace(field_meta.get_name(), offset);
        auto field_sizeof = field_meta.get_sizeof();
        sizeof_infos_.push_back(field_sizeof);
        total_sizeof_ += field_sizeof;
    }

    auto
    begin() {
        return fields_.begin();
    }

    auto
    end() {
        return fields_.end();
    }
    auto
    begin() const {
        return fields_.begin();
    }

    auto
    end() const {
        return fields_.end();
    }

    int
    size() const {
        return fields_.size();
    }

    const FieldMeta&
    operator[](int field_index) const {
        Assert(field_index >= 0);
        Assert(field_index < fields_.size());
        return fields_[field_index];
    }

    auto
    get_total_sizeof() const {
        return total_sizeof_;
    }

    const std::vector<int>&
    get_sizeof_infos() {
        return sizeof_infos_;
    }

    std::optional<int>
    get_offset(const std::string& field_name) {
        if (!offsets_.count(field_name)) {
            return std::nullopt;
        } else {
            return offsets_[field_name];
        }
    }

    const std::vector<FieldMeta>&
    get_fields() {
        return fields_;
    }

    const FieldMeta&
    operator[](const std::string& field_name) const {
        auto offset_iter = offsets_.find(field_name);
        AssertInfo(offset_iter != offsets_.end(), "Cannot found field_name: " + field_name);
        auto offset = offset_iter->second;
        return (*this)[offset];
    }

 private:
    // this is where data holds
    std::vector<FieldMeta> fields_;

 private:
    // a mapping for random access
    std::unordered_map<std::string, int> offsets_;
    std::vector<int> sizeof_infos_;
    int total_sizeof_ = 0;
};

using SchemaPtr = std::shared_ptr<Schema>;
using idx_t = int64_t;

}  // namespace milvus