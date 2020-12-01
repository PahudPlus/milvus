// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License

#include <iostream>
#include "EasyAssert.h"
// #define BOOST_STACKTRACE_USE_ADDR2LINE
#define BOOST_STACKTRACE_USE_BACKTRACE
#include <boost/stacktrace.hpp>

namespace milvus::impl {
void
EasyAssertInfo(
    bool value, std::string_view expr_str, std::string_view filename, int lineno, std::string_view extra_info) {
    if (!value) {
        std::string info;
        info += "Assert \"" + std::string(expr_str) + "\"";
        info += " at " + std::string(filename) + ":" + std::to_string(lineno) + "\n";
        if (!extra_info.empty()) {
            info += " => " + std::string(extra_info);
        }
        auto fuck = boost::stacktrace::stacktrace();
        std::cout << fuck;
        // std::string s = fuck;
        // info += ;
        throw std::runtime_error(info);
    }
}
}  // namespace milvus::impl