/*
 * Copyright 2017-2019 Yuji Ito <llamerada.jp@gmail.com>
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

#include "system_2d.hpp"

namespace colonio {

System2DDelegate::~System2DDelegate() {
}

System2DBase::System2DBase(Context& context, ModuleDelegate& module_delegate,
                           System2DDelegate& system_delegate, ModuleChannel::Type channel) :
    Module(context, module_delegate, channel),
    delegate(system_delegate) {
}

const NodeID& System2DBase::get_relay_nid(const Coordinate& position) {
  return delegate.system_2d_do_get_relay_nid(position);
}
}  // namespace colonio