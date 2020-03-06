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

#include "api_entry_bundler.hpp"
#include "colonio_impl.hpp"
#include "context.hpp"
#include "logger.hpp"
#include "scheduler.hpp"

namespace colonio {
class SideModule;

class SideModuleDelegate {
 public:
  virtual ~SideModuleDelegate();
  virtual void side_module_on_event(SideModule& sm, std::unique_ptr<api::Event> event) = 0;
  virtual void side_module_on_reply(SideModule& sm, std::unique_ptr<api::Reply> reply) = 0;
  virtual void side_module_on_require_invoke(SideModule& sm, unsigned int msec)        = 0;
};

class SideModule : public LoggerDelegate, public SchedulerDelegate, public APIEntryDelegate {
 public:
  Logger logger;

  SideModule(SideModuleDelegate& delegate_);
  virtual ~SideModule();

  void call(const api::Call& call);
  unsigned int invoke();

 private:
  SideModuleDelegate& delegate;
  Scheduler scheduler;
  Context context;
  APIEntryBundler bundler;
  std::shared_ptr<ColonioImpl> colonio_impl;

  void api_entry_send_event(APIEntry& entry, std::unique_ptr<api::Event> event) override;
  void api_entry_send_reply(APIEntry& entry, std::unique_ptr<api::Reply> reply) override;

  void logger_on_output(Logger& logger, LogLevel::Type level, const std::string& message) override;

  void scheduler_on_require_invoke(Scheduler& sched, unsigned int msec) override;
};
}  // namespace colonio