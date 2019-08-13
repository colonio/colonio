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
#pragma once

#include <functional>

#include "core/system_1d.hpp"
#include "colonio/map.hpp"

namespace colonio {
typedef uint32_t PAXOS_N;

class MapPaxos : public System1D<Map> {
 public:
  MapPaxos(Context& context, ModuleDelegate& module_delegate, System1DDelegate& system_delegate,
           const picojson::object& config);
  virtual ~MapPaxos();

  void get(const Value& key,
           const std::function<void(const Value&)>& on_success,
           const std::function<void(MapFailureReason)>& on_failure) override;
  void set(const Value& key, const Value& value,
           const std::function<void()>& on_success,
           const std::function<void(MapFailureReason)>& on_failure,
           MapOption::Type opt = 0x0) override;

  void system_1d_on_change_nearby(const NodeID& prev_nid, const NodeID& next_nid) override;

 private:
  class AcceptorInfo {
   public:
    PAXOS_N na;
    PAXOS_N np;
    PAXOS_N ia;
    Value value;
    NodeID last_nid;

    AcceptorInfo();
    AcceptorInfo(PAXOS_N na_, PAXOS_N np_, PAXOS_N ia_, const Value& value_);
  };

  struct ProposerInfo {
    PAXOS_N np;
    PAXOS_N ip;
    bool reset;
    Value value;
    uint32_t processing_packet_id;

    ProposerInfo();
    ProposerInfo(PAXOS_N np_, PAXOS_N ip_, const Value& value_);
  };

  class CommandGet : public Command {
   public:
    class Info {
     public:
      std::unique_ptr<Value> key;
      int count_retry;
      // (n, i), value
      std::map<std::tuple<PAXOS_N, PAXOS_N>, Value> ok_values;
      // (n, i), count
      std::map<std::tuple<PAXOS_N, PAXOS_N>, int> ok_counts;

      MapPaxos& parent;
      int count_ng;
      bool is_finished;
      std::function<void(const Value&)> cb_on_success;
      std::function<void(MapFailureReason)> cb_on_failure;

      Info(MapPaxos& parent_, std::unique_ptr<Value> key_, int count_retry_);
    };

    std::shared_ptr<Info> info;

    CommandGet(std::shared_ptr<Info> info_);

    void on_error(const std::string& message) override;
    void on_failure(std::unique_ptr<const Packet> packet) override;
    void on_success(std::unique_ptr<const Packet> packet) override;
    const std::tuple<PAXOS_N, PAXOS_N, Value>* get_best();
    void postprocess();
  };

  class CommandSet : public Command {
   public:
    class Info {
     public:
      std::function<void()> cb_on_success;
      std::function<void(MapFailureReason)> cb_on_failure;
      const Value key;
      const Value value;
      const MapOption::Type opt;
      MapPaxos& parent;

      Info(MapPaxos& parent_, const Value& key_, const Value& value_,
           const std::function<void()>& cb_on_success_,
           const std::function<void(MapFailureReason)>& cb_on_failure_,
           const MapOption::Type& opt_);
    };
    std::unique_ptr<Info> info;

    CommandSet(std::unique_ptr<Info> info_);

    void on_error(const std::string& message) override;
    void on_failure(std::unique_ptr<const Packet> packet) override;
    void on_success(std::unique_ptr<const Packet> packet) override;
  };

  class CommandPrepare : public Command {
   public:
    class Reply {
     public:
      NodeID src_nid;
      PAXOS_N n;
      PAXOS_N i;
      bool is_success;

      Reply(const NodeID& src_nid_, PAXOS_N n_, PAXOS_N i_, bool is_success_);
    };

    class Info {
     public:
      std::unique_ptr<const Packet> packet_reply;
      std::unique_ptr<Value> key;
      PAXOS_N n_max;
      PAXOS_N i_max;
      MapOption::Type opt;
      MapPaxos& parent;
      std::vector<Reply> replys;
      bool is_finished;
      Info(MapPaxos& parent_,
           std::unique_ptr<const Packet> packet_reply_,
           std::unique_ptr<Value>  key_,
           MapOption::Type opt_);
      virtual ~Info();
    };

    std::shared_ptr<Info> info;

    CommandPrepare(std::shared_ptr<Info> info_);

    void on_error(const std::string& message) override;
    void on_failure(std::unique_ptr<const Packet> packet) override;
    void on_success(std::unique_ptr<const Packet> packet) override;
    void postprocess();
  };

  class CommandAccept : public Command {
   public:
    class Reply {
     public:
      NodeID src_nid;
      PAXOS_N n;
      PAXOS_N i;
      bool is_success;

      Reply(const NodeID& src_nid_, PAXOS_N n_, PAXOS_N i_, bool is_success_);
    };

    class Info {
     public:
      std::unique_ptr<const Packet> packet_reply;
      std::unique_ptr<Value> key;
      PAXOS_N n_max;
      PAXOS_N i_max;
      MapOption::Type opt;
      MapPaxos& parent;
      std::vector<Reply> replys;
      bool is_finished;

      Info(MapPaxos& parent_,
           std::unique_ptr<const Packet> packet_reply_,
           std::unique_ptr<Value>  key_,
           MapOption::Type opt_);
      virtual ~Info();
    };

    std::shared_ptr<Info> info;

    CommandAccept(std::shared_ptr<Info> info_);

    void on_error(const std::string& message) override;
    void on_failure(std::unique_ptr<const Packet> packet) override;
    void on_success(std::unique_ptr<const Packet> packet) override;
    void postprocess();
  };

  unsigned int conf_retry_max;

  std::string solt;
  std::map<Value, AcceptorInfo> acceptor_infos;
  std::map<Value, ProposerInfo> proposer_infos;

  MapPaxos(const MapPaxos&);
  MapPaxos& operator=(const MapPaxos&);

  void module_process_command(std::unique_ptr<const Packet> packet) override;

  bool check_key_acceptor(const Value& key);
  bool check_key_proposer(const Value& key);
  void recv_packet_accept(std::unique_ptr<const Packet> packet);
  void recv_packet_balance_acceptor(std::unique_ptr<const Packet> packet);
  void recv_packet_balance_proposer(std::unique_ptr<const Packet> packet);
  void recv_packet_get(std::unique_ptr<const Packet> packet);
  void recv_packet_hint(std::unique_ptr<const Packet> packet);
  void recv_packet_prepare(std::unique_ptr<const Packet> packet);
  void recv_packet_set(std::unique_ptr<const Packet> packet);
  void send_packet_accept(ProposerInfo& proposer, std::unique_ptr<const Packet> packet_reply,
                          std::unique_ptr<Value> key, MapOption::Type opt);
  void send_packet_balance_acceptor(const Value& key, const AcceptorInfo& acceptor);
  void send_packet_balance_proposer(const Value& key, const ProposerInfo& proposer);
  void send_packet_get(std::unique_ptr<Value> key, int count_retry,
                       const std::function<void(const Value&)>& on_success,
                       const std::function<void(MapFailureReason)>& on_failure);
  void send_packet_hint(const Value& key, const Value& value, PAXOS_N n, PAXOS_N i);
  void send_packet_prepare(ProposerInfo& proposer, std::unique_ptr<const Packet> packet_reply,
                           std::unique_ptr<Value> key, MapOption::Type opt);
  void send_packet_set(std::unique_ptr<CommandSet::Info> info);

#ifndef NDEBUG
  void debug_on_change_set();
#endif
};
}  // namespace colonio