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

#include <exception>
#include <string>

namespace colonio {
class ColonioException : public std::exception {
 public:
  enum class Code : uint32_t {
    UNDEFINED,
    SYSTEM_ERROR,
    OFFLINE,
    CONFLICT_WITH_SETTING,
    NOT_EXIST_KEY,
    // EXIST_KEY,
    CHANGED_PROPOSER,
    COLLISION_LATE
  };

  Code code;
  /// A message string for display or bug report.
  const std::string message;

  explicit ColonioException(Code code_, const std::string& message_);

  /**
   * Pass message without line-no and file name.
   */
  const char* what() const noexcept override;
};
}  // namespace colonio