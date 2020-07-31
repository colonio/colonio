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
#include "context.hpp"

#include <cassert>

#include "coord_system.hpp"
#include "logger.hpp"

namespace colonio {

Context::Context(Logger& logger_, Scheduler& scheduler_) :
    logger(logger_), scheduler(scheduler_), local_nid(NodeID::make_random()) {
}

}  // namespace colonio
