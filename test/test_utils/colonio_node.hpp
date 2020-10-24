/*
 * Copyright 2017-2020 Yuji Ito <llamerada.jp@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <cstdio>

#include "colonio/colonio.hpp"

class ColonioNode : public colonio::Colonio {
 public:
  explicit ColonioNode(const std::string& node_name_) : Colonio(), node_name(node_name_) {
  }

 protected:
  std::string node_name;

  void on_output_log(const std::string& json) override {
    // Implement log output method to check.
    printf("%s %s\n", node_name.c_str(), json.c_str());
  }
};
