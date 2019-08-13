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
#include <cassert>

#include "map_paxos_protocol.pb.h"

#include "core/convert.hpp"
#include "core/definition.hpp"
#include "core/utils.hpp"
#include "core/value_impl.hpp"
#include "map_paxos.hpp"

namespace colonio {
static const unsigned int NUM_ACCEPTOR = 3;
static const unsigned int NUM_MAJORITY = 2;

/* class KVSPaxos::AcceptorInfo */
MapPaxos::AcceptorInfo::AcceptorInfo() :
    na(0),
    np(0),
    ia(0) {
}

MapPaxos::AcceptorInfo::AcceptorInfo(PAXOS_N na_, PAXOS_N np_, PAXOS_N ia_, const Value& value_) :
    na(na_),
    np(np_),
    ia(ia_),
    value(value_) {
}

/* class MapPaxos::ProposerInfo */
MapPaxos::ProposerInfo::ProposerInfo() :
    np(0),
    ip(0),
    reset(true),
    processing_packet_id(PACKET_ID_NONE) {
}

MapPaxos::ProposerInfo::ProposerInfo(PAXOS_N np_, PAXOS_N ip_, const Value& value_) :
    np(np_),
    ip(ip_),
    reset(true),
    value(value_),
    processing_packet_id(PACKET_ID_NONE) {
}

/* class MapPaxos::CommandGet::Info */
MapPaxos::CommandGet::Info::Info(MapPaxos& parent_, std::unique_ptr<Value> key_, int count_retry_) :
    key(std::move(key_)),
    count_retry(count_retry_),
    parent(parent_),
    count_ng(0),
    is_finished(false) {
}

/* class MapPaxos::CommandGet */
MapPaxos::CommandGet::CommandGet(std::shared_ptr<Info> info_) :
    Command(CommandID::MapPaxos::GET, PacketMode::NONE),
    info(info_) {
}

void MapPaxos::CommandGet::on_error(const std::string& message) {
  logD(info->parent.context, "Error on packet of 'get'.(message=%s)", message.c_str());
  info->count_ng += 1;

  postprocess();
}

void MapPaxos::CommandGet::on_failure(std::unique_ptr<const Packet> packet) {
  info->count_ng += 1;

  postprocess();
}

void MapPaxos::CommandGet::on_success(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::GetSuccess content;
  packet->parse_content(&content);
  std::tuple<PAXOS_N, PAXOS_N> key = std::make_tuple(content.n(), content.i());
  if (info->ok_values.find(key) == info->ok_values.end()) {
    info->ok_values.insert(std::make_pair(key, ValueImpl::from_pb(content.value())));
    info->ok_counts.insert(std::make_pair(key, 1));
  } else {
    info->ok_counts.at(key) ++;
  }

  postprocess();
}

void MapPaxos::CommandGet::postprocess() {
  if (info->is_finished == true) {
    return;
  }

  int ok_sum = 0;
  for (auto ok_count : info->ok_counts) {
    if (ok_count.second >= NUM_MAJORITY) {
      info->is_finished = true;
      info->cb_on_success(info->ok_values.at(ok_count.first));
      return;
    }
    ok_sum += ok_count.second;
  }

  if (ok_sum + info->count_ng == NUM_ACCEPTOR) {
    logD(info->parent.context, "map get retry(%d/%d, sum:%d)", info->count_retry, info->parent.conf_retry_max, ok_sum);
    info->is_finished = true;

    if (ok_sum == 0) {
      if (info->count_retry < info->parent.conf_retry_max) {
        info->parent.send_packet_get(std::move(info->key), info->count_retry + 1,
                                     info->cb_on_success, info->cb_on_failure);
      } else {
        info->cb_on_failure(MapFailureReason::NOT_EXIST_KEY);
      }

    } else {
      PAXOS_N n = 0;
      PAXOS_N i = 0;
      Value* value = nullptr;
      for (auto& it : info->ok_values) {
        if (value == nullptr ||
            n < std::get<0>(it.first) ||
            (n == std::get<0>(it.first) && i < std::get<1>(it.first))) {
          n = std::get<0>(it.first);
          i = std::get<1>(it.first);
          value = &(it.second);
        }
      }
      
      info->parent.send_packet_hint(*info->key, *value, n, i);
      // @todo Set interval before retry to get command.
      info->parent.send_packet_get(std::move(info->key), info->count_retry + 1,
                                   info->cb_on_success, info->cb_on_failure);
    }
  }
}

/* class MapPaxos::CommandSet::Info */
MapPaxos::CommandSet::Info::Info(MapPaxos& parent_, const Value& key_, const Value& value_,
                                 const std::function<void()>& cb_on_success_,
                                 const std::function<void(MapFailureReason)>& cb_on_failure_,
                                 const MapOption::Type& opt_) :
    cb_on_success(cb_on_success_),
    cb_on_failure(cb_on_failure_),
    key(key_),
    value(value_),
    opt(opt_),
    parent(parent_) {
}

/* class MapPaxos::CommandSet */
MapPaxos::CommandSet::CommandSet(std::unique_ptr<MapPaxos::CommandSet::Info> info_) :
    Command(CommandID::MapPaxos::SET, PacketMode::NONE),
    info(std::move(info_)) {
}

void MapPaxos::CommandSet::on_error(const std::string& message) {
  logD(info->parent.context, "Error on packet of 'set'.(message=%s)", message.c_str());
  info->cb_on_failure(MapFailureReason::SYSTEM_ERROR);
}

void MapPaxos::CommandSet::on_failure(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::SetFailure content;
  packet->parse_content(&content);
  const MapFailureReason reason = static_cast<MapFailureReason>(content.reason());

  if (reason == MapFailureReason::CHANGED_PROPOSER) {
    info->parent.send_packet_set(std::move(info));
    
  } else {
    info->cb_on_failure(reason);
  }
}

void MapPaxos::CommandSet::on_success(std::unique_ptr<const Packet> packet) {
  info->cb_on_success();
}

/* class MapPaxos::CommandPrepare::Reply */
MapPaxos::CommandPrepare::Reply::Reply(const NodeID& src_nid_, PAXOS_N n_, PAXOS_N i_, bool is_success_) :
    src_nid(src_nid_),
    n(n_),
    i(i_),
    is_success(is_success_) {
}

/* class MapPaxos::CommandPrepare::Info */
MapPaxos::CommandPrepare::Info::Info(MapPaxos& parent_,
                                     std::unique_ptr<const Packet> packet_reply_,
                                     std::unique_ptr<Value>  key_,
                                     MapOption::Type opt_) :
    packet_reply(std::move(packet_reply_)),
    key(std::move(key_)),
    n_max(0),
    i_max(0),
    opt(opt_),
    parent(parent_),
    is_finished(false) {
}

MapPaxos::CommandPrepare::Info::~Info() {
  if (!is_finished) {
    auto proposer_it = parent.proposer_infos.find(*key);
    if (proposer_it != parent.proposer_infos.end()) {
      ProposerInfo& proposer = proposer_it->second;
      proposer.processing_packet_id = PACKET_ID_NONE;
    }
  }
}

/* class MapPaxos::CommandPrepare */
MapPaxos::CommandPrepare::CommandPrepare(std::shared_ptr<MapPaxos::CommandPrepare::Info> info_) :
    Command(CommandID::MapPaxos::PREPARE, PacketMode::NONE),
    info(info_) {
}

void MapPaxos::CommandPrepare::on_error(const std::string& message) {
  logD(info->parent.context, "Error on packet of 'prepare'.(message=%s)", message.c_str());
  info->replys.push_back(Reply(NodeID::NONE, 0, 0, false));

  postprocess();
}

void MapPaxos::CommandPrepare::on_failure(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::PrepareFailure content;
  packet->parse_content(&content);

  info->replys.push_back(Reply(packet->src_nid, content.n(), 0, false));

  postprocess();
}

void MapPaxos::CommandPrepare::on_success(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::PrepareSuccess content;
  packet->parse_content(&content);

  info->replys.push_back(Reply(packet->src_nid, content.n(), content.i(), true));

  postprocess();
}

void MapPaxos::CommandPrepare::postprocess() {
  if (info->is_finished) {
    return;
  }

  auto proposer_it = info->parent.proposer_infos.find(*info->key);
  if (proposer_it == info->parent.proposer_infos.end()) {
    switch (info->packet_reply->command_id) {
      case CommandID::MapPaxos::SET: {
        MapPaxosProtocol::SetFailure param;
        param.set_reason(static_cast<uint32_t>(MapFailureReason::CHANGED_PROPOSER));
        info->parent.send_failure(*info->packet_reply, Module::serialize_pb(param));
      } break;

      case CommandID::MapPaxos::HINT: {
        // Do nothing.
      } break;

      default:
        assert(false);
    }
    return;
  }
  ProposerInfo& proposer = proposer_it->second;

  for (const auto& src : info->replys) {
    if (src.is_success) {
      for (auto& target : info->replys) {
        if (src.src_nid == target.src_nid &&
            src.n == target.n) {
          target.is_success = true;
        }
      }
    }
  }
  int count_ok = 0;
  int count_ng = 0;
  for (const auto& it : info->replys) {
    if (it.is_success) {
      count_ok ++;
      if (it.i > info->i_max) {
        info->i_max = it.i;
      }
  
    } else {
      count_ng ++;
      if (it.n > info->n_max) {
        info->n_max = it.n;
      }
    }
  }

  if (count_ok >= NUM_MAJORITY) {
    info->is_finished = true;
    proposer.ip = info->i_max + 1;
    
    info->parent.send_packet_accept(proposer, std::move(info->packet_reply),
                                    std::move(info->key), info->opt);

  } else if (count_ng >= NUM_MAJORITY) {
    info->is_finished = true;
    proposer.np = info->n_max + 1;
    proposer.reset = true;

    info->parent.send_packet_prepare(proposer, std::move(info->packet_reply),
                                     std::move(info->key), info->opt);
  }
}

/* class MapPaxos::CommandAccept::Reply */
MapPaxos::CommandAccept::Reply::Reply(const NodeID& src_nid_, PAXOS_N n_, PAXOS_N i_, bool is_success_) :
    src_nid(src_nid_),
    n(n_),
    i(i_),
    is_success(is_success_) {
}

/* class MapPaxos::CommandAccept::Info */
MapPaxos::CommandAccept::Info::Info(MapPaxos& parent_,
                                    std::unique_ptr<const Packet> packet_reply_,
                                    std::unique_ptr<Value>  key_,
                                    MapOption::Type opt_) :
    packet_reply(std::move(packet_reply_)),
    key(std::move(key_)),
    n_max(0),
    i_max(0),
    opt(opt_),
    parent(parent_),
    is_finished(false) {
}

MapPaxos::CommandAccept::Info::~Info() {
  if (!is_finished) {
    auto proposer_it = parent.proposer_infos.find(*key);
    if (proposer_it != parent.proposer_infos.end()) {
      ProposerInfo& proposer = proposer_it->second;
      proposer.processing_packet_id = PACKET_ID_NONE;
    }
  }
}

/* class MapPaxos::CommandAccept */
MapPaxos::CommandAccept::CommandAccept(std::shared_ptr<MapPaxos::CommandAccept::Info> info_) :
    Command(CommandID::MapPaxos::ACCEPT, PacketMode::NONE),
    info(info_) {
}

void MapPaxos::CommandAccept::on_error(const std::string& message) {
  logD(info->parent.context, "Error on packet of 'accept'.(message=%s)", message.c_str());
  info->replys.push_back(Reply(NodeID::NONE, 0, 0, false));

  postprocess();
}

void MapPaxos::CommandAccept::on_failure(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::AcceptFailure content;
  packet->parse_content(&content);

  info->replys.push_back(Reply(packet->src_nid, content.n(), content.i(), false));

  postprocess();
}

void MapPaxos::CommandAccept::on_success(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::AcceptSuccess content;
  packet->parse_content(&content);

  info->replys.push_back(Reply(packet->src_nid, content.n(), content.i(), true));

  postprocess();
}

void MapPaxos::CommandAccept::postprocess() {
  if (info->is_finished) {
    return;
  }

  auto proposer_it = info->parent.proposer_infos.find(*info->key);
  if (proposer_it == info->parent.proposer_infos.end()) {
    switch (info->packet_reply->command_id) {
      case CommandID::MapPaxos::SET: {
        MapPaxosProtocol::SetFailure param;
        param.set_reason(static_cast<uint32_t>(MapFailureReason::CHANGED_PROPOSER));
        info->parent.send_failure(*info->packet_reply, Module::serialize_pb(param));
      } break;

      case CommandID::MapPaxos::HINT: {
        // Do nothing.
      } break;

      default:
        assert(false);
    }
    return;
  }
  ProposerInfo& proposer = proposer_it->second;

  for (const auto& src : info->replys) {
    if (src.is_success) {
      for (auto& target : info->replys) {
        if (src.src_nid == target.src_nid &&
            src.n == target.n &&
            src.i == target.i) {
          target.is_success = true;
        }
      }
    }
  }
  int count_ok = 0;
  int count_ng = 0;
  for (const auto& it : info->replys) {
    if (it.is_success) {
      count_ok ++;
      if (it.i > info->i_max) {
        info->i_max = it.i;
      }

    } else {
      count_ng ++;
      if (it.n > info->n_max) {
        info->n_max = it.n;
      }
    }
  }

  if (count_ok >= NUM_MAJORITY) {
    assert(proposer.processing_packet_id == info->packet_reply->id ||
           proposer.processing_packet_id == PACKET_ID_NONE);
    info->is_finished = true;
    proposer.ip = info->i_max + 1;
    proposer.reset = false;
    proposer.processing_packet_id = PACKET_ID_NONE;

    switch (info->packet_reply->command_id) {
      case CommandID::MapPaxos::SET: {
        info->parent.send_success(*info->packet_reply, nullptr);
      } break;

      case CommandID::MapPaxos::HINT: {
        // Do nothing.
      } break;

      default:
        assert(false);
    }

  } else if (count_ng >= NUM_MAJORITY) {
    info->is_finished = true;
    proposer.np = info->n_max + 1;
    proposer.reset = true;

    info->parent.send_packet_prepare(proposer, std::move(info->packet_reply),
                                     std::move(info->key), info->opt);
  }
}

/* class KeyValueStore */
MapPaxos::MapPaxos(Context& context, ModuleDelegate& module_delegate, System1DDelegate& system_delegate,
                   const picojson::object& config) :
    System1D(context, module_delegate, system_delegate, Utils::get_json<double>(config, "channel")),
    conf_retry_max(MAP_PAXOS_RETRY_MAX) {
  Utils::check_json_optional(config, "retryMax", &conf_retry_max);
}

MapPaxos::~MapPaxos() {
}

void MapPaxos::get(const Value& key,
                   const std::function<void(const Value&)>& on_success,
                   const std::function<void(MapFailureReason)>& on_failure) {
  send_packet_get(std::make_unique<Value>(key), 0, on_success, on_failure);
}

void MapPaxos::set(const Value& key, const Value& value,
                   const std::function<void()>& on_success,
                   const std::function<void(MapFailureReason)>& on_failure, MapOption::Type opt) {
  std::unique_ptr<CommandSet::Info> info =
    std::make_unique<CommandSet::Info>(*this, key, value, on_success, on_failure, opt);
  send_packet_set(std::move(info));
}

void MapPaxos::system_1d_on_change_nearby(const NodeID& prev_nid, const NodeID& next_nid) {
  auto acceptor_it = acceptor_infos.begin();
  while (acceptor_it != acceptor_infos.end()) {
    const Value& key = acceptor_it->first;
    if (!check_key_acceptor(key)) {
      send_packet_balance_acceptor(key, acceptor_it->second);
      acceptor_it = acceptor_infos.erase(acceptor_it);

    } else {
      acceptor_it ++;
    }
  }

  auto proposer_it = proposer_infos.begin();
  while (proposer_it != proposer_infos.end()) {
    const Value& key = proposer_it->first;
    if (!check_key_proposer(key)) {
      send_packet_balance_proposer(key, proposer_it->second);
      proposer_it = proposer_infos.erase(proposer_it);

    } else {
      proposer_it ++;
    }
  }

#ifndef NDEBUG
  debug_on_change_set();
#endif
}

void MapPaxos::module_process_command(std::unique_ptr<const Packet> packet) {
  switch(packet->command_id) {
    case CommandID::MapPaxos::GET:
      recv_packet_get(std::move(packet));
      break;

    case CommandID::MapPaxos::SET:
      recv_packet_set(std::move(packet));
      break;

    case CommandID::MapPaxos::PREPARE:
      recv_packet_prepare(std::move(packet));
      break;

    case CommandID::MapPaxos::ACCEPT:
      recv_packet_accept(std::move(packet));
      break;

    case CommandID::MapPaxos::HINT:
      recv_packet_hint(std::move(packet));
      break;

    case CommandID::MapPaxos::BALANCE_ACCEPTOR:
      recv_packet_balance_acceptor(std::move(packet));
      break;

    case CommandID::MapPaxos::BALANCE_PROPOSER:
      recv_packet_balance_proposer(std::move(packet));
      break;

    default:
      // TODO(llamerada.jp@gmail.com) Warning on recving invalid packet.
      assert(false);
  }
}

bool MapPaxos::check_key_acceptor(const Value& key) {
  NodeID hash = ValueImpl::to_hash(key, solt);
  for (int i = 0; i < NUM_ACCEPTOR; i++) {
    hash += NodeID::QUARTER;
    if (system_1d_check_coverd_range(hash)) {
      return true;
    }
  }
  return false;
}

bool MapPaxos::check_key_proposer(const Value& key) {
  NodeID hash = ValueImpl::to_hash(key, solt);
  return system_1d_check_coverd_range(hash);
}

#ifndef NDEBUG
void MapPaxos::debug_on_change_set() {
  picojson::array a;
  for (auto& pi : proposer_infos) {
    picojson::object o;
    o.insert(std::make_pair("key", picojson::value(ValueImpl::to_str(pi.first))));
    o.insert(std::make_pair("value", picojson::value(ValueImpl::to_str(pi.second.value))));
    o.insert(std::make_pair("hash", picojson::value(ValueImpl::to_hash(pi.first, solt).to_str())));
    a.push_back(picojson::value(o));
  }

  context.debug_event(DebugEvent::MAP_SET, picojson::value(a));
}
#endif

void MapPaxos::recv_packet_accept(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::Accept content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  Value value = ValueImpl::from_pb(content.value());
  const PAXOS_N n = content.n();
  const PAXOS_N i = content.i();

  auto acceptor_it = acceptor_infos.find(key);
  if (acceptor_it == acceptor_infos.end()) {
    if (check_key_acceptor(key)) {
      bool r;
      std::tie(acceptor_it, r) = acceptor_infos.insert(std::make_pair(key, AcceptorInfo()));
#ifndef NDEBUG
      debug_on_change_set();
#endif

    } else {
      // ignore
      logd("Receive 'accept' packet at wrong node.(key=%s)", ValueImpl::to_str(key).c_str());
      return;
    }
  }

  AcceptorInfo& acceptor = acceptor_it->second;
  if (n >= acceptor.np) {
    if (packet->src_nid != acceptor.last_nid) {
      acceptor.ia = 0;
    }
    if (i > acceptor.ia) {
      acceptor.na = acceptor.np = n;
      acceptor.ia = i;
      acceptor.value = value;
      acceptor.last_nid = packet->src_nid;

      MapPaxosProtocol::AcceptSuccess param;
      param.set_n(acceptor.np);
      param.set_i(acceptor.ia);
      send_success(*packet, serialize_pb(param));
      return;
    }
  }

  MapPaxosProtocol::AcceptFailure param;
  param.set_n(acceptor.np);
  param.set_i(acceptor.ia);
  send_failure(*packet, serialize_pb(param));
}

void MapPaxos::recv_packet_hint(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::Hint content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  Value value = ValueImpl::from_pb(content.value());
  const PAXOS_N n = content.n();
  const PAXOS_N i = content.i();

  if (check_key_proposer(key)) {
    auto proposer_it = proposer_infos.find(key);
    if (proposer_it == proposer_infos.end()) {
      bool r;
      std::tie(proposer_it, r) = proposer_infos.insert(std::make_pair(key, ProposerInfo()));
      assert(r);
    }

    ProposerInfo& proposer = proposer_it->second;
    if (n > proposer.np) {
      proposer.np += 1;
      proposer.value = value;
    }
    // Same logic with set command.
    if (proposer.reset) {
      logd("Prepare.(id=%s)", Convert::int2str(packet->id).c_str());
      send_packet_prepare(proposer,
                          Module::copy_packet_for_reply(*packet),
                          std::make_unique<Value>(key), MapOption::NONE);

    } else {
      logd("Accept.(id=%s)", Convert::int2str(packet->id).c_str());
      send_packet_accept(proposer,
                         Module::copy_packet_for_reply(*packet),
                         std::make_unique<Value>(key), MapOption::NONE);
    }
        
  } else {
    // ignore
    logd("Receive 'hint' packet at wrong node.(key=%s)", ValueImpl::to_str(key).c_str());
    return;
  }
}

void MapPaxos::recv_packet_balance_acceptor(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::BalanceAcceptor content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  Value value = ValueImpl::from_pb(content.value());
  const PAXOS_N na = content.na();
  const PAXOS_N np = content.np();
  const PAXOS_N ia = content.ia();

  if (check_key_acceptor(key)) {
    auto acceptor_it = acceptor_infos.find(key);
    if (acceptor_it == acceptor_infos.end()) {
      acceptor_infos.insert(std::make_pair(key, AcceptorInfo(na, np, ia, value)));

    } else {
      AcceptorInfo& acceptor = acceptor_it->second;
      if (np > acceptor.np || (np == acceptor.np && ia > acceptor.ia)) {
        acceptor.na = na;
        acceptor.np = np;
        acceptor.ia = ia;
        acceptor.value = value;
        acceptor.last_nid = NodeID::NONE;
      }
    }
  }
}

void MapPaxos::recv_packet_balance_proposer(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::BalanceProposer content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  Value value = ValueImpl::from_pb(content.value());
  const PAXOS_N np = content.np();
  const PAXOS_N ip = content.ip();

  if (check_key_proposer(key)) {
    auto proposer_it = proposer_infos.find(key);
    if (proposer_it == proposer_infos.end()) {
      proposer_infos.insert(std::make_pair(key, ProposerInfo(np, ip, value)));
#ifndef NDEBUG
      debug_on_change_set();
#endif

    } else {
      ProposerInfo& proposer = proposer_it->second;
      if (proposer.processing_packet_id == PACKET_ID_NONE &&
          (np > proposer.np || (np == proposer.np && ip > proposer.ip))) {
        proposer.np = np;
        proposer.ip = ip;
        proposer.value = value;
      }
    }
  }
}

void MapPaxos::recv_packet_get(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::Get content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());

  auto acceptor_it = acceptor_infos.find(key);
  if (acceptor_it == acceptor_infos.end() ||
      acceptor_it->second.na == 0) {
    // TODO(llamerada.jp@gmail.com) Search data from another accetpors.
    send_failure(*packet, std::shared_ptr<std::string>());

  } else {
    AcceptorInfo& acceptor_info = acceptor_it->second;
    MapPaxosProtocol::GetSuccess param;
    param.set_n(acceptor_info.na);
    param.set_i(acceptor_info.ia);
    ValueImpl::to_pb(param.mutable_value(), acceptor_info.value);
    send_success(*packet, serialize_pb(param));
  }
}

void MapPaxos::recv_packet_prepare(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::Prepare content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  const PAXOS_N n = content.n();
  const MapOption::Type opt  = content.opt();

  auto acceptor_it = acceptor_infos.find(key);
  if (acceptor_it == acceptor_infos.end()) {
    if (check_key_acceptor(key)) {
      bool r;
      std::tie(acceptor_it, r) = acceptor_infos.insert(std::make_pair(key, AcceptorInfo()));
      assert(r);

    } else {
      // ignore
      logd("Receive 'prepare' packet at wrong node.(key=%s)", ValueImpl::to_str(key).c_str());
      return;
    }
  }

  AcceptorInfo& acceptor = acceptor_it->second;

  if (n > acceptor.np) {
    acceptor.np = n;
    PAXOS_N i;

    if (packet->src_nid != acceptor.last_nid) {
      i = 0;
    } else {
      i = acceptor.ia;
    }

    MapPaxosProtocol::PrepareSuccess param;
    param.set_n(n);
    param.set_i(i);
    send_success(*packet, serialize_pb(param));
      
  } else {
    MapPaxosProtocol::PrepareFailure param;
    param.set_n(acceptor.np);
    send_failure(*packet, serialize_pb(param));
  }
}

void MapPaxos::recv_packet_set(std::unique_ptr<const Packet> packet) {
  MapPaxosProtocol::Set content;
  packet->parse_content(&content);
  Value key = ValueImpl::from_pb(content.key());
  Value value = ValueImpl::from_pb(content.value());
  const MapOption::Type opt  = content.opt();

  auto proposer_it = proposer_infos.find(key);
  if (proposer_it == proposer_infos.end()) {
    if (check_key_proposer(key)) {
      bool r;
      std::tie(proposer_it, r) = proposer_infos.insert(std::make_pair(key, ProposerInfo()));
      assert(r);
#ifndef NDEBUG
      debug_on_change_set();
#endif

    } else {
      // ignore
      logd("Receive 'set' packet at wrong node.(key=%s)", ValueImpl::to_str(key).c_str());
      return;
    }
  }

  ProposerInfo& proposer = proposer_it->second;
  proposer.np += 1;
  proposer.value = value;

  if (proposer.processing_packet_id != PACKET_ID_NONE) {
    // @todo switch by flag
    MapPaxosProtocol::SetFailure param;
    param.set_reason(static_cast<uint32_t>(MapFailureReason::COLLISION_LATE));
    send_failure(*packet, serialize_pb(param));

  } else {
    proposer.processing_packet_id = packet->id;

    // Same logic with hint command.
    if (proposer.reset) {
      send_packet_prepare(proposer,
                          Module::copy_packet_for_reply(*packet),
                          std::make_unique<Value>(key), opt);

    } else {
      send_packet_accept(proposer,
                         Module::copy_packet_for_reply(*packet),
                         std::make_unique<Value>(key), opt);
    }
  }
}

void MapPaxos::send_packet_accept(ProposerInfo& proposer, std::unique_ptr<const Packet> packet_reply,
                                  std::unique_ptr<Value> key, MapOption::Type opt) {
  std::shared_ptr<CommandAccept::Info>
    accept_info = std::make_shared<CommandAccept::Info>(*this, std::move(packet_reply),
                                                        std::move(key), opt);

  MapPaxosProtocol::Accept param;
  ValueImpl::to_pb(param.mutable_key(), *accept_info->key);
  ValueImpl::to_pb(param.mutable_value(), proposer.value);
  param.set_n(proposer.np);
  param.set_i(proposer.ip);
  std::shared_ptr<const std::string> param_bin = serialize_pb(param);

  NodeID acceptor_nid = ValueImpl::to_hash(*accept_info->key, solt);
  for (int i = 0; i < NUM_ACCEPTOR; i++) {
    acceptor_nid += NodeID::QUARTER;
    std::unique_ptr<Command> command = std::make_unique<CommandAccept>(accept_info);
    send_packet(std::move(command), acceptor_nid, param_bin);
  }
}

void MapPaxos::send_packet_balance_acceptor(const Value& key, const AcceptorInfo& acceptor) {
  MapPaxosProtocol::BalanceAcceptor param;
  ValueImpl::to_pb(param.mutable_key(), key);
  ValueImpl::to_pb(param.mutable_value(), acceptor.value);
  param.set_na(acceptor.na);
  param.set_np(acceptor.np);
  param.set_ia(acceptor.ia);
  std::shared_ptr<const std::string> param_bin = serialize_pb(param);

  NodeID acceptor_nid = ValueImpl::to_hash(key, solt);
  for (int i = 0; i < NUM_ACCEPTOR; i++) {
    acceptor_nid += NodeID::QUARTER;
    send_packet(acceptor_nid, PacketMode::ONE_WAY, CommandID::MapPaxos::BALANCE_ACCEPTOR, param_bin);
  }
}

void MapPaxos::send_packet_balance_proposer(const Value& key, const ProposerInfo& proposer) {
  MapPaxosProtocol::BalanceProposer param;
  ValueImpl::to_pb(param.mutable_key(), key);
  ValueImpl::to_pb(param.mutable_value(), proposer.value);
  param.set_np(proposer.np);
  param.set_ip(proposer.ip);

  NodeID proposer_nid = ValueImpl::to_hash(key, solt);
  send_packet(proposer_nid, PacketMode::ONE_WAY, CommandID::MapPaxos::BALANCE_PROPOSER, serialize_pb(param));
}

void MapPaxos::send_packet_get(std::unique_ptr<Value> key, int count_retry,
                               const std::function<void(const Value&)>& on_success,
                               const std::function<void(MapFailureReason)>& on_failure) {
  std::shared_ptr<CommandGet::Info> info = std::make_unique<CommandGet::Info>(*this, std::move(key), count_retry);
  info->cb_on_success = on_success;
  info->cb_on_failure = on_failure;

  MapPaxosProtocol::Get param;
  ValueImpl::to_pb(param.mutable_key(), *info->key);
  std::shared_ptr<const std::string> param_bin = serialize_pb(param);

  NodeID acceptor_nid = ValueImpl::to_hash(*info->key, solt);

  for (int i = 0; i < NUM_ACCEPTOR; i++) {
    acceptor_nid += NodeID::QUARTER;
    std::unique_ptr<Command> command = std::make_unique<CommandGet>(info);

    send_packet(std::move(command), acceptor_nid, param_bin);
  }
}

void MapPaxos::send_packet_hint(const Value& key, const Value& value, PAXOS_N n, PAXOS_N i) {
  MapPaxosProtocol::Hint param;
  ValueImpl::to_pb(param.mutable_key(), key);
  ValueImpl::to_pb(param.mutable_value(), value);
  param.set_n(n);
  param.set_i(i);

  NodeID proposer_nid = ValueImpl::to_hash(key, solt);
  send_packet(proposer_nid, PacketMode::ONE_WAY, CommandID::MapPaxos::HINT, serialize_pb(param));
}

void MapPaxos::send_packet_prepare(ProposerInfo& proposer, std::unique_ptr<const Packet> packet_reply,
                                   std::unique_ptr<Value> key, MapOption::Type opt) {
  std::shared_ptr<CommandPrepare::Info> prepare_info =
    std::make_unique<CommandPrepare::Info>(*this, std::move(packet_reply), std::move(key), opt);

  MapPaxosProtocol::Prepare param;
  ValueImpl::to_pb(param.mutable_key(), *prepare_info->key);
  param.set_n(proposer.np);
  param.set_opt(opt);
  std::shared_ptr<const std::string> param_bin = serialize_pb(param);

  NodeID acceptor_nid = ValueImpl::to_hash(*prepare_info->key, solt);
  for (int i = 0; i < NUM_ACCEPTOR; i++) {
    acceptor_nid += NodeID::QUARTER;
    std::unique_ptr<Command> command = std::make_unique<CommandPrepare>(prepare_info);
    send_packet(std::move(command), acceptor_nid, param_bin);
  }
}

void MapPaxos::send_packet_set(std::unique_ptr<CommandSet::Info> info) {
  MapPaxosProtocol::Set param;
  ValueImpl::to_pb(param.mutable_key(), info->key);
  ValueImpl::to_pb(param.mutable_value(), info->value);
  param.set_opt(info->opt);

  NodeID proposer_nid = ValueImpl::to_hash(info->key, solt);
  std::unique_ptr<Command> command = std::make_unique<CommandSet>(std::move(info));
  send_packet(std::move(command), proposer_nid, serialize_pb(param));
}
}  // namespace colonio