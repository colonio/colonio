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

#include "pubsub_2d_api.hpp"

#include "core/api_bundler.hpp"
#include "core/module_bundler.hpp"
#include "core/utils.hpp"
#include "core/value_impl.hpp"
#include "pubsub_2d_module.hpp"

namespace colonio {

void Pubsub2DAPI::make_entry(
    Context& context, APIBundler& api_bundler, APIDelegate& api_delegate, ModuleBundler& module_bundler,
    const CoordSystem& coord_system, const picojson::object& config) {
  APIChannel::Type channel = static_cast<APIChannel::Type>(Utils::get_json<double>(config, "channel"));
  uint32_t cache_time      = Utils::get_json<double>(config, "cacheTime", PUBSUB_2D_CACHE_TIME);

  std::shared_ptr<Pubsub2DAPI> entry(new Pubsub2DAPI(context, api_delegate, channel));
  std::unique_ptr<Pubsub2DModule> module = std::make_unique<Pubsub2DModule>(
      context, module_bundler.module_delegate, module_bundler.module_2d_delegate, *entry, coord_system, channel,
      ModuleChannel::Pubsub2D::PUBSUB_2D, cache_time);

  module_bundler.registrate(module.get(), false, true);
  entry->module = std::move(module);
  api_bundler.registrate(entry);
}

Pubsub2DAPI::Pubsub2DAPI(Context& context, APIDelegate& delegate, APIChannel::Type channel) :
    APIBase(context, delegate, channel) {
}

void Pubsub2DAPI::pubsub_2d_module_on_on(Pubsub2DModule& ps2_module, const std::string& name, const Value& value) {
  std::unique_ptr<api::Event> ev = std::make_unique<api::Event>();
  ev->set_channel(channel);
  api::pubsub_2d::OnEvent* on_event = ev->mutable_pubsub_2d_on();
  on_event->set_name(name);
  ValueImpl::to_pb(on_event->mutable_value(), value);

  api_event(std::move(ev));
}

void Pubsub2DAPI::api_on_recv_call(const api::Call& call) {
  switch (call.param_case()) {
    case api::Call::ParamCase::kPubsub2DPublish: {
      const api::pubsub_2d::Publish& param = call.pubsub_2d_publish();
      api_publish(
          call.id(), param.name(), param.x(), param.y(), param.r(), ValueImpl::from_pb(param.value()), param.opt());
    } break;

    default:
      colonio_fatal("Called incorrect colonio API entry : %d", call.param_case());
      break;
  }
}

void Pubsub2DAPI::api_publish(
    uint32_t id, const std::string& name, double x, double y, double r, const Value& value, uint32_t opt) {
  module->publish(
      name, x, y, r, value, opt,
      [this, id]() {
        //
        api_success(id);
      },
      [this, id](ErrorCode code) {
        // TODO error message
        api_failure(id, code, "");
      });
}
}  // namespace colonio
